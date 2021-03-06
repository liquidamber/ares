#include <cmath>
#include "util.hpp"
#include "croute.h"
#include "cdatabase.h"
#include "cfare.h"

namespace ares
{
  namespace
  {
    const double FARE_TAX = 1.05;
    const int FARE_KILO_HONSHU_MAIN[] = {0, 300, 600};
    const double FARE_RATE_HONSHU_MAIN[] = {16.2, 12.85, 7.05};

    inline int round(int val)
    {
      int base = static_cast<int>(val * FARE_TAX) + 5;
      return base - base % 10;
    }

    template<class MainLineLookupFunction,
             class LocalLineLookupFunction>
    int calc_fare_as_honshu(const CDatabase & db,
                            const CKilo & kilo,
                            const boost::optional<JR_COMPANY_TYPE> comp_type,
                            MainLineLookupFunction func_main,
                            LocalLineLookupFunction func_local)
    {
      const CHecto hecto_main  =
        comp_type ? kilo.get(*comp_type, true ) : kilo.get_all_JR(true);
      const CHecto hecto_local =
        comp_type ? kilo.get(*comp_type, false) : kilo.get_all_JR(false);
      const CHecto hecto_local_fake = comp_type
        ? kilo.get(*comp_type, false, false) : kilo.get_all_JR(false, false);
      // Main only
      if(hecto_local == 0)
      {
        const DENSHA_SPECIAL_TYPE denshaid = kilo.get_densha_and_circleid();
        const char * faretable=nullptr;
        switch(denshaid)
        {
        case DENSHA_SPECIAL_TOKYO:      faretable = "D1"; break;
        case DENSHA_SPECIAL_OSAKA:      faretable = "D2"; break;
        case DENSHA_SPECIAL_YAMANOTE:   faretable = "E1"; break;
        case DENSHA_SPECIAL_OSAKAKANJO: faretable = "E2"; break;
        default:
          return func_main(hecto_main);
        }
        return db.get_fare_table(faretable,
                                 COMPANY_HONSHU,
                                 hecto_main);
      }
      // Local only or (Main+Local)<=10
      if(hecto_main == 0 || (hecto_main + hecto_local) <= 10)
      {
        return func_local(hecto_main + hecto_local);
      }
      // Both main & local
      const int hecto_total =
        hecto_main + hecto_local_fake;
      return func_main(hecto_total);
    }
  }

  std::ostream & operator<<(std::ostream & ost, const ares::CRoute & route)
  {
    if(route.way.empty()) return ost;
    for(size_t i=0; i<route.way.size(); ++i)
    {
      if(i==0)
      {
        ost << route.db->get_station_name(route.way[i].begin);
      }
      else
      {
        ost << "[" << route.db->get_station_name(route.way[i].begin) << "]";
      }
      ost << ",";
      ost << route.db->get_line_name(route.way[i].line) << ",";
    }
    ost << route.db->get_station_name(route.way.back().end);
    return ost;
  }

  bool CRoute::operator==(const CRoute & b) const {
    const CRoute & a = *this;
    return a.way == b.way;
  }

  void CRoute::init()
  {
    $.way.clear();
  }

  void CRoute::init(station_id_t station)
  {
    $.init();
    $.way.push_back(CSegment{station});
  }

  bool CRoute::append_route(line_id_t line, station_id_t station)
  {
    if(way.empty()){ std::cerr << "2-arg toward empty route\n"; return false; }
    if(way.back().is_begin())
    {
      way.back().line = line;
      way.back().end = station;
    }
    else {
      way.push_back(CSegment(way.back().end, line, station));
    }
    return true;
  }

  bool CRoute::append_route(const char * line, const char * station)
  {
    return $.append_route(db->get_lineid(line),
                          db->get_stationid(station));
  }

  bool CRoute::append_route(line_id_t line, station_id_t begin, station_id_t end)
  {
    if(true
       && !way.empty()
       && way.back().is_begin())
    { return false; }
    way.push_back(CSegment(begin, line, end));
    return true;
  }

  bool CRoute::append_route(const char * line, const char * begin, const char * end)
  {
    return $.append_route(db->get_lineid(line),
                          db->get_stationid(begin),
                          db->get_stationid(end));
  }

  bool CRoute::is_contains(station_id_t station) const
  {
    return $.end() !=
      std::find_if($.begin(), $.end(),
                   [station, this](const CSegment & segment)
                   { return $.db->is_contains(segment, station); });
  }

  bool CRoute::is_valid() const
  {
    // Connectivity: each segment tail = next segment head
    if($.end() !=
       std::adjacent_find($.begin(), $.end(),
                          [] (const CSegment & a, const CSegment & b)
                          { return a.end != b.begin; }))
    { return false; }
    // No same station
    station_vector stations;
    for(const auto & segment : $)
    {
      size_t size = $.db->get_stations_of_segment(segment.line,
                                                  segment.begin,
                                                  segment.end,
                                                  stations);
      if(size != 0) { stations.pop_back(); }
    }
    std::sort(stations.begin(), stations.end());
    if(stations.end() !=
       std::adjacent_find(stations.begin(), stations.end()))
    { return false; }
    return true;
  }

  void CRoute::canonicalize()
  {
    WayContainer tmp;
    for(auto curr=$.begin(); curr != $.end(); ++curr)
    {
      auto prev=tmp.rbegin();
      // connecting prev & curr
      if(!tmp.empty()
         && prev->line == curr->line
         && prev->end == curr->begin)
      {
        const auto range1 = $.db->get_range(prev->line,
                                            prev->begin, prev->end);
        const auto range2 = $.db->get_range(curr->line,
                                            curr->begin, curr->end);
        if((range1.first < range1.second && range2.first < range2.second) ||
           (range1.first > range1.second && range2.first > range2.second))
        {
          prev->end = curr->end;
          continue;
        }
      }
      tmp.push_back(*curr);
    }
    $.way = std::move(tmp);
  }

  class CFare CRoute::accum() const
  {
    CFare fare;
    CKilo & kilo = fare.kilo;
    std::vector<CKiloValue> result;
    for(auto itr=$.begin(); itr != $.end(); ++itr)
    {
      bool is_main;
      DENSHA_SPECIAL_TYPE denshaid, circleid;
      result.clear();
      boost::optional<std::pair<bool, int> > special
        = $.db->get_special_fare(itr->line, itr->begin, itr->end);
      // ! is_add
      if(special && !special->first)
      {
        fare.other += special->second;
      }
      else
      {
        // is_add
        if(special && special->first) { fare.JR += special->second; }
        bool ret = $.db->get_company_and_kilo(itr->line,
                                              itr->begin,
                                              itr->end,
                                              result,
                                              is_main,
                                              denshaid,
                                              circleid);
        assert(ret);
        kilo.update_denshaid(denshaid, circleid);
        std::for_each(result.begin(), result.end(),
                      [&kilo, is_main] (const CKiloValue & a)
                      { kilo.add(a.company, is_main, a.begin, a.end); });
      }
    }
    return fare;
  }

  /*
   * @note この実装は三島会社同士で直接連絡できないことを前提にしている.
   * 例えば大分から愛媛のJR路線が出来れば面倒, なんてね.
   */
  int CRoute::calc_fare_inplace()
  {
    using namespace std::placeholders;
    // Error checking, returning -1 is not good, boost::optional is better.
    if(!$.is_valid()) { return -1; }
    // Canonicalize route.
    $.canonicalize();
    // Rewrite Route: shinkansen / route-variant
    // Get Kilo: Additional fare should included in CKilo
    CFare fare = $.accum();
    const CKilo & kilo = fare.kilo;
    if(!kilo.is_zero(COMPANY_KTR))
    {
      fare.other += $.db->get_fare_table("Z", COMPANY_KTR,
                                         kilo.get(COMPANY_KTR, true));
    }
    // 0キロ
    if(kilo.is_all_JR_zero()) { return fare; }
    // Get only company.
    boost::optional<JR_COMPANY_TYPE> only = kilo.get_only_JR();
    // JR四国 or JR九州
    if(only && (*only == JR_COMPANY_KYUSHU || *only == JR_COMPANY_SHIKOKU))
    {
      const CHecto hecto_main  = kilo.get(*only, true );
      const CHecto hecto_local = kilo.get(*only, false);
      const CHecto hecto_lfake = kilo.get(*only, false, false);
      // only 幹線
      if(hecto_local == 0)
      {
        fare.JR += $.db->get_fare_table("C1", *only, hecto_main);
        return fare;
      }
      // only 地方交通線
      else if(hecto_main == 0)
      {
        boost::optional<int> special_fare =
          $.db->get_fare_country_table("C2", *only, hecto_local, hecto_lfake);
        if(special_fare)
        {
          fare.JR += *special_fare;
          return fare;
        }
        fare.JR += $.db->get_fare_table("C1", *only, hecto_lfake);
        return fare;
      }
      else
      {
        boost::optional<int> special_fare =
          $.db->get_fare_country_table("C3", *only,
                                       hecto_main + hecto_local,
                                       hecto_main + hecto_lfake);
        if(special_fare)
        {
          fare.JR += *special_fare;
          return fare;
        }
        fare.JR += $.db->get_fare_table("C1", *only, hecto_main + hecto_lfake);
        return fare;
      }
    }
    // JR北海道
    else if(only && (*only == JR_COMPANY_HOKKAIDO))
    {
      fare.JR += calc_fare_as_honshu(*db, kilo,
                                     JR_COMPANY_HOKKAIDO,
                                     std::bind(&CDatabase::get_fare_table,
                                               $.db, "C1", COMPANY_HOKKAIDO, _1),
                                     std::bind(&CDatabase::get_fare_table,
                                               $.db, "B1", COMPANY_HOKKAIDO, _1));
      return fare;
    }
    // 本州含み
    else
    {
      const int base_fare =
        calc_fare_as_honshu(*db, kilo,
                            boost::none,
                            std::bind(CRoute::calc_honshu_main, _1),
                            std::bind(&CDatabase::get_fare_table,
                                      $.db, "B1", COMPANY_HONSHU, _1));
      int add_fare = 0;
      for(size_t i=JR_COMPANY_HOKKAIDO; i < MAX_JR_COMPANY_TYPE; ++i)
      {
        if(!kilo.is_zero(i))
          add_fare +=
            calc_fare_as_honshu(*db, kilo,
                                JR_COMPANY_TYPE(i),
                                std::bind(&CDatabase::get_fare_table,
                                          $.db, "A2", i, _1),
                                std::bind(&CDatabase::get_fare_table,
                                          $.db, "B2", i, _1));
      }
      fare.JR += base_fare + add_fare;
      return fare;
    }
  }

  int CRoute::calc_honshu_main(int kilo)
  {
    // hard-coded.
    // I should use SQLite database.
    if (kilo <= 3)
      return 140;
    else if (kilo <=6)
      return 180;
    else if (kilo <=10)
      return 190;

    // align kilo.
    if (kilo <= 50)
      kilo = (kilo-1)/5*5+3;
    else if (kilo <= 100)
      kilo = (kilo-1)/10*10+5;
    else if (kilo <= 600)
      kilo = (kilo-1)/20*20+10;
    else
      kilo = (kilo-1)/40*40+20;

    // sum fare.
    double sum[] = {0, 0, 0};
    sum[0] = (kilo < FARE_KILO_HONSHU_MAIN[1])
      ? FARE_RATE_HONSHU_MAIN[0] * kilo
      : FARE_RATE_HONSHU_MAIN[0] * FARE_KILO_HONSHU_MAIN[1];
    if (kilo <= FARE_KILO_HONSHU_MAIN[1])
      sum[1] =  0;
    else if (kilo < FARE_KILO_HONSHU_MAIN[2])
      sum[1] = FARE_RATE_HONSHU_MAIN[1] * (kilo - FARE_KILO_HONSHU_MAIN[1]);
    else
      sum[1] = FARE_RATE_HONSHU_MAIN[1] * (FARE_KILO_HONSHU_MAIN[2] - FARE_KILO_HONSHU_MAIN[1]);
    sum[2] = (kilo <= FARE_KILO_HONSHU_MAIN[2]) ? 0 :
      FARE_RATE_HONSHU_MAIN[2] * (kilo - FARE_KILO_HONSHU_MAIN[2]);

    const double totalsum = sum[0] + sum[1] + sum[2];
    if(kilo < 100)
    {
      return round(static_cast<int>(std::ceil(totalsum/10)*10));
    }
    else
    {
      const int isum = static_cast<int>(totalsum) + 50;
      return round(isum - isum % 100);
    }
  }
}
