#include <cmath>
#include "util.hpp"
#include "croute.h"
#include "cdatabase.h"

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

  void CRoute::append_route(line_id_t line, station_id_t station)
  {
    if(way.back().is_begin())
    {
      way.back().line = line;
      way.back().end = station;
    }
    else {
      way.push_back(CSegment(way.back().end, line, station));
    }
  }

  void CRoute::append_route(line_id_t line, station_id_t begin, station_id_t end)
  {
    way.push_back(CSegment(begin, line, end));
  }

  bool CRoute::is_contains(station_id_t station) const
  {
    for(auto itr = $.begin(); itr != $.end(); ++itr)
    {
      if($.db->is_contains(*itr, station)) return true;
    }
    return false;
  }

  bool CRoute::is_valid() const
  {
    // Connectivity: each segment tail = next segment head
    for(auto itr=$.begin()+1; itr != $.end(); ++itr)
    {
      if((itr-1)->end != itr->begin){ return false; }
    }
    // NoOverlap   : segments does not overlap
    std::map<line_id_t, liquid::UniqueIntervalTree<station_id_t> > checktree;
    for(auto itr=$.begin(); itr != $.end(); ++itr)
    {
      if(!checktree[itr->line].insert(itr->begin, itr->end)) { return false; }
    }
    return true;
  }

  class CKilo CRoute::get_kilo() const
  {
    CKilo kilo;
    std::vector<std::pair<company_id_t, line_id_t> > result;
    for(auto itr=$.begin(); itr != $.end(); ++itr)
    {
      bool is_main;
      result.clear();
      bool ret = $.db->get_company_and_kilo(itr->line,
                                            itr->begin,
                                            itr->end,
                                            result,
                                            is_main);
      if(!ret) { return CKilo(); }
      for(auto j=result.begin(); j != result.end(); ++j)
      {
        kilo.add(j->first, is_main, j->second);
      }
    }
    return kilo;
  }

  int CRoute::calc_fare_inplace()
  {
    // Error checking, returning -1 is not good, boost::optional is better.
    if(!$.is_valid()) { return -1; }
    // Rewrite Route: shinkansen / route-variant
    // Get Kilo
    CKilo kilo($.get_kilo());
    // Calc
    // Check if honshu main or not!!!
    return calc_honshu_main(kilo.get_kilo(COMPANY_HONSHU, true));
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
