#pragma once

#include <string>
#include <iostream>
#include <stdexcept>
#include <boost/utility.hpp>
#include <boost/optional.hpp>
#include <sqlite3.h>

namespace sqlite3_wrapper
{
  /**
   * SQLite全般の例外
   */
  class SQLiteException : std::runtime_error
  {
  public:
    SQLiteException(const std::string & str) : std::runtime_error(str) {}
  };

  /**
   * @~english
   * IO Error Exception.
   */
  /**
   * @~japanese
   * IOエラー例外.
   */
  class IOException : SQLiteException
  {
  public:
    IOException(const std::string & str) : SQLiteException(str) {}
  };

  /**
   * @~english
   * Wrapper object of sqlite3 object.
   */
  /**
   * @~japanese
   * sqlite3オブジェクトのラッパークラス.
   */
  class SQLite : boost::noncopyable
  {
  private:
    sqlite3 * db;
  public:
    /**
     * @~english
     * Constructor with filname and options.
     * @param[in] dbname database filename.
     * @param[in] flags database file open mode.
     * @param[in] zVfs Name of VFS module to use. Refer SQLite document.
     */
    /**
     * @~japanese
     * 通常のコンストラクタ.
     * @param[in] データベースのファイル名.
     * @param[in] データベースを開くときのオプション.
     * @param[in] VFSモジュールのzVfs名を指定. SQLiteのドキュメント読め.
     */
    SQLite(const char * dbname,
	   int flags=SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
	   const char *zVfs=NULL)
    {
      int rc;
      rc = sqlite3_open_v2(dbname, &db, flags, zVfs);
      if(rc != SQLITE_OK)
      {
        std::string err = sqlite3_errmsg(db);
        sqlite3_close(db);
        throw IOException(err);
      }
    }

    ~SQLite()
    {
      sqlite3_close(db);
    }

    /**
     * Return sqlite3 error message.
     * @return pointer to UTF-8 error message string.
     */
    const char * errmsg()
    {
      return sqlite3_errmsg(db);
    }

    friend class SQLiteStmt;

  private:
    /**
     * Return rare pointer of sqlite3 object.
     * This is bad know-how.
     * @return pointer to sqlite3 object.
     */
    sqlite3 * ptr() const
    {
      return db;
    }
  };

  /**
   * Wrapper object of SQLite statement, sqlite3_stmt.
   */
  class SQLiteStmt : boost::noncopyable
  {
  private:
    sqlite3_stmt * stmt;
    SQLite & db;
  public:
    /**
     * Constructor with SQL statement string of char*.
     * @param[in] db       SQLite object to tie up with.
     * @param[in] query    Query string of char *.
     * @param[in] qlength  Size of query in bytes.
     */
    SQLiteStmt(SQLite & db, const char * query, size_t qlength)
      : db(db)
    {
      sqlite3_prepare_v2(db.ptr(), query, qlength, &stmt, NULL);
      this->reset();
    }

    /**
     * Constructor with SQL statement string of std::string.
     * @param[in] db    SQLite object to tie up with.
     * @param[in] query Query string of std::string.
     */
    SQLiteStmt(SQLite & db, const std::string & query)
      : db(db)
    {
      sqlite3_prepare_v2(db.ptr(), query.c_str(), query.size(), &stmt, NULL);
      this->reset();
    }

    ~SQLiteStmt()
    {
      sqlite3_finalize(stmt);
    }

    /**
     * Function to bind integer value to place holder.
     * @param[in] icol    Place holder's column index starting from 1.
     * @param[in] value   Value to bind to place holder.
     * @return            State of rare sqlite function.
     */
    int bind(int icol, int value)
    {
      return sqlite3_bind_int(stmt, icol, value);
    }

    /**
     * Function to bind double value to place holder.
     * @param[in] icol    Place holder's column index starting from 1.
     * @param[in] value   Value to bind to place holder.
     * @return            State of rare sqlite function.
     */
    int bind(int icol, double value)
    {
      return sqlite3_bind_double(stmt, icol, value);
    }

    /**
     * Function to bind double value to place holder.
     * @param[in] icol    Place holder's column index starting from 1.
     * @param[in] value   Pointer to string binding to place holder.
     * @param[in] vlength Length of the string.
     * @return            State of rare sqlite function.
     */
    int bind(int icol, const char * value, int vlength)
    {
      return sqlite3_bind_text(stmt, icol, value, vlength, SQLITE_STATIC);
    }

    /**
     * Function to bind double value to place holder.
     * @param[in] icol       Place holder's column index starting from 1.
     * @param[in] value      Pointer to string binding to place holder.
     * @param[in] vlength    Length of the string.
     * @param[in] destructor Deallocator for string called from SQLite API.
     * @return            State of rare sqlite function.
     */
    int bind(int icol, const char * value, int vlength, void (*destructor)(void*))
    {
      return sqlite3_bind_text(stmt, icol, value, vlength, destructor);
    }

    /**
     * Function to bind double value to place holder.
     * @param[in] icol    Place holder's column index starting from 1.
     * @param[in] value   Pointer to string binding to place holder.
     * @return            State of rare sqlite function.
     */
    int bind(int icol, const std::string & value)
    {
      return sqlite3_bind_text(stmt, icol, value.c_str(), value.size(), SQLITE_STATIC);
    }

    /**
     * Function to bind NULL to place holder.
     * @param[in] icol place holder's column index starting from 1.
     * @return state of rare sqlite function.
     */
    int bind_null(int icol)
    {
      return sqlite3_bind_null(stmt, icol);
    }

    /**
     * Function to get the number of place holders.
     * @return the number of place holders.
     */
    int bind_parameter_count()
    {
      return sqlite3_bind_parameter_count(stmt);
    }

    /**
     * Function to get place holder's index from its name.
     */
    int bind_parameter_index(const char *zName)
    {
      return sqlite3_bind_parameter_index(stmt, zName);
    }

    /**
     * Function to get place holder's name from its index.
     */
    const char * bind_parameter_name(int n)
    {
      return sqlite3_bind_parameter_name(stmt, n);
    }

    /**
     * Function to clear all binded values.
     */
    int clear_bindings()
    {
      return sqlite3_clear_bindings(stmt);
    }

    /**
     * Class to convert from column index to various types.
     */
    struct column_value
    {
    private:
      const SQLiteStmt * t;
      int _icol;
    public:
      //! Constructor.
      column_value(const SQLiteStmt * t, int icol) : t(t), _icol(icol){}
      /**
       * Check if column is NULL or not. Call before getting value.
       */
      bool is_null() const
      {
        return (sqlite3_column_type(t->stmt, _icol) == SQLITE_NULL);
      }
      //! cast operator to int.
      operator int()
      {
        return sqlite3_column_int(t->stmt, _icol);
      }
      //! cast operator to long long int.
      operator long long int()
      {
        return sqlite3_column_int64(t->stmt, _icol);
      }
      //! cast operator to char *.
      operator const char *()
      {
        return reinterpret_cast<const char *>(sqlite3_column_text(t->stmt, _icol));
      }
      //! cast operator to std::string.
      operator std::string()
      {
        return std::string(reinterpret_cast<const char *>(sqlite3_column_text(t->stmt, _icol)));
      }
    };

    /**
     * Function to get column value of specified index.
     */
    column_value column(int icol) const
    {
      return column_value(this, icol);
    }

    /**
     * Function to step SQL statement execution.
     */
    int step()
    {
      int rc = sqlite3_step(stmt);
      if(rc != SQLITE_ROW && rc != SQLITE_DONE)
      {
        std::string err = db.errmsg();
        throw SQLiteException(err);
      }
      return rc;
    }

    /**
     * Function to reset SQL statement execution.
     */
    int reset()
    {
      return sqlite3_reset(stmt);
    }

    class iterator
    {
    private:
      boost::optional<SQLiteStmt &> stmt;

      void step()
      {
        if(!stmt)
        {
          throw SQLiteException("uninitialized iterator of SQLiteStmt");
        }
        int rc = stmt->step();
        if(rc == SQLITE_DONE) { stmt = boost::none; }
      }

    public:
      /**
       * デフォルトコンストラクタ.
       * 無効なイテレータを生成する.
       */
      iterator() {}

      /**
       * コンストラクタ.
       * ステートメントに対するイテレータを生成する.
       * @param[in] stmt 対応するSQLiteStmtオブジェクト.
       */
      explicit iterator(SQLiteStmt & stmt) : stmt(stmt) { step(); }

      //! 一致演算子. イテレータが終端であるかどうかだけを比較する.
      friend bool operator==(const iterator & a, const iterator & b)
      {
        return (a.stmt && b.stmt) || (!a.stmt && !b.stmt);
      }

      friend bool operator!=(const iterator & a, const iterator & b)
      {
        return !(a == b);
      }

      //! イテレータに対応するステートメントを実行し結果を更新する.
      iterator & operator++() { step(); return *this; }

      operator bool() { return static_cast<bool>(stmt); }

      //! 指定されたカラムの値を取り出す.
      SQLiteStmt::column_value operator[](int icol)
      {
        if(!stmt)
        {
          throw SQLiteException("uninitialized iterator of SQLiteStmt");
        }
        return stmt->column(icol);
      }
    };

    iterator execute()
    {
      reset();
      return iterator(*this);
    }

    /**
     * Function to append icol's value to vec.
     * @param[in,out] vec    STL container to append icol's value.
     * @param[in]     icol   Index of the column to append.
     */
    template <class T>
    void fill_column(T & vec, int icol)
    {
      for(iterator itr=this->execute(); itr; ++itr)
      {
        vec.push_back(itr[icol]);
      }
    }

    /**
     * Function to append icol & jcol's value to vec.
     * @param[in,out] vec    STL container to append icol's value.
     * @param[in]     icol   Index of the column to append as first.
     * @param[in]     jcol   Index of the column to append as second.
     */
    template <class T>
    void fill_column(T & vec, int icol, int jcol)
    {
      for(iterator itr=this->execute(); itr; ++itr)
      {
        vec.push_back(std::make_pair(itr[icol],
                                     itr[jcol]));
      }
    }
  };
}
