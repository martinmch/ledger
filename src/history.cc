/*
 * Copyright (c) 2003-2012, John Wiegley.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * - Neither the name of New Artisans LLC nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <system.hh>

#include "history.h"

template <typename T>
struct f_max : public std::binary_function<T, T, bool> {
  T operator()(const T& x, const T& y) const {
    return std::max(x, y);
  }
};

namespace ledger {

template <typename EdgeWeightMap,
          typename PricePointMap,
          typename PriceRatioMap>
class recent_edge_weight
{
public:
  EdgeWeightMap weight;
  PricePointMap price_point;
  PriceRatioMap ratios;

  datetime_t reftime;
  datetime_t oldest;

  recent_edge_weight() { }
  recent_edge_weight(EdgeWeightMap     _weight,
                     PricePointMap     _price_point,
                     PriceRatioMap     _ratios,
                     const datetime_t& _reftime,
                     const datetime_t& _oldest = datetime_t())
    : weight(_weight), price_point(_price_point), ratios(_ratios),
      reftime(_reftime), oldest(_oldest) { }

  template <typename Edge>
  bool operator()(const Edge& e) const
  {
#if defined(DEBUG_ON)
    DEBUG("history.find", "  reftime      = " << reftime);
    if (! oldest.is_not_a_date_time()) {
      DEBUG("history.find", "  oldest       = " << oldest);
    }
#endif

    const price_map_t& prices(get(ratios, e));
    if (prices.empty()) {
      DEBUG("history.find", "  prices map is empty for this edge");
      return false;
    }

    price_map_t::const_iterator low = prices.upper_bound(reftime);
    if (low != prices.end() && low == prices.begin()) {
      DEBUG("history.find", "  don't use this edge");
      return false;
    } else {
      --low;
      assert(((*low).first <= reftime));

      if (! oldest.is_not_a_date_time() && (*low).first < oldest) {
        DEBUG("history.find", "  edge is out of range");
        return false;
      }

      long secs = (reftime - (*low).first).total_seconds();
      assert(secs >= 0);

      put(weight, e, secs);
      put(price_point, e, price_point_t((*low).first, (*low).second));

      DEBUG("history.find", "  using edge at price point "
            << (*low).first << " " << (*low).second);
      return true;
    }
  }
};

typedef filtered_graph
  <commodity_history_t::Graph,
   recent_edge_weight<commodity_history_t::EdgeWeightMap,
                      commodity_history_t::PricePointMap,
                      commodity_history_t::PriceRatioMap> > FGraph;

typedef property_map<FGraph, vertex_name_t>::type FNameMap;
typedef property_map<FGraph, vertex_index_t>::type FIndexMap;
typedef iterator_property_map
  <commodity_history_t::vertex_descriptor*, FIndexMap,
   commodity_history_t::vertex_descriptor,
   commodity_history_t::vertex_descriptor&> FPredecessorMap;
typedef iterator_property_map<long*, FIndexMap, long, long&> FDistanceMap;

void commodity_history_t::add_commodity(commodity_t& comm)
{
  if (! comm.graph_index()) {
    comm.set_graph_index(num_vertices(price_graph));
    add_vertex(/* vertex_name= */ &comm, price_graph);
  }
}

void commodity_history_t::add_price(const commodity_t& source,
                                    const datetime_t&  when,
                                    const amount_t&    price)
{
  vertex_descriptor sv = vertex(*source.graph_index(), price_graph);
  vertex_descriptor tv = vertex(*price.commodity().graph_index(), price_graph);

  std::pair<edge_descriptor, bool> e1 = edge(sv, tv, price_graph);
  if (! e1.second)
    e1 = add_edge(sv, tv, price_graph);

  price_map_t& prices(get(ratiomap, e1.first));

  std::pair<price_map_t::iterator, bool> result =
    prices.insert(price_map_t::value_type(when, price));
  if (! result.second) {
    // There is already an entry for this moment, so update it
    (*result.first).second = price;
  }
}

void commodity_history_t::remove_price(const commodity_t& source,
                                       const commodity_t& target,
                                       const datetime_t&  date)
{
  vertex_descriptor sv = vertex(*source.graph_index(), price_graph);
  vertex_descriptor tv = vertex(*target.graph_index(), price_graph);

  std::pair<Graph::edge_descriptor, bool> e1 = edge(sv, tv, price_graph);
  if (e1.second) {
    price_map_t& prices(get(ratiomap, e1.first));

    // jww (2012-03-04): If it fails, should we give a warning?
    prices.erase(date);

    if (prices.empty())
      remove_edge(e1.first, price_graph);
  }
}

void commodity_history_t::map_prices(function<void(datetime_t,
                                                   const amount_t&)> fn,
                                     const commodity_t& source,
                                     const datetime_t&  moment,
                                     const datetime_t&  oldest)
{
  vertex_descriptor sv = vertex(*source.graph_index(), price_graph);

  FGraph fg(price_graph,
            recent_edge_weight<EdgeWeightMap, PricePointMap, PriceRatioMap>
            (get(edge_weight, price_graph), pricemap, ratiomap,
             moment, oldest));

  FNameMap namemap(get(vertex_name, fg));

  graph_traits<FGraph>::adjacency_iterator f_vi, f_vend;
  for (tie(f_vi, f_vend) = adjacent_vertices(sv, fg); f_vi != f_vend; ++f_vi) {
    std::pair<Graph::edge_descriptor, bool> edgePair = edge(sv, *f_vi, fg);
    Graph::edge_descriptor edge = edgePair.first;

    const price_map_t& prices(get(ratiomap, edge));

    foreach (const price_map_t::value_type& pair, prices) {
      const datetime_t& when(pair.first);

      if ((oldest.is_not_a_date_time() || when >= oldest) && when <= moment) {
        if (pair.second.commodity() == source) {
          amount_t price(pair.second);
          price.in_place_invert();
          if (source == *get(namemap, sv))
            price.set_commodity(const_cast<commodity_t&>(*get(namemap, *f_vi)));
          else
            price.set_commodity(const_cast<commodity_t&>(*get(namemap, sv)));
        }
        fn(when, pair.second);
      }
    }
  }
}

optional<price_point_t>
commodity_history_t::find_price(const commodity_t& source,
                                const datetime_t&  moment,
                                const datetime_t&  oldest)
{
  vertex_descriptor sv = vertex(*source.graph_index(), price_graph);

  FGraph fg(price_graph,
            recent_edge_weight<EdgeWeightMap, PricePointMap, PriceRatioMap>
            (get(edge_weight, price_graph), pricemap, ratiomap,
             moment, oldest));

  FNameMap namemap(get(vertex_name, fg));

  DEBUG("history.find", "sv commodity = " << get(namemap, sv)->symbol());
#if defined(DEBUG_ON)
  if (source.has_flags(COMMODITY_PRIMARY))
    DEBUG("history.find", "sv commodity is primary");
#endif
  DEBUG("history.find", "tv commodity = none ");

  datetime_t most_recent = moment;
  amount_t   price;

  graph_traits<FGraph>::adjacency_iterator f_vi, f_vend;
  for (tie(f_vi, f_vend) = adjacent_vertices(sv, fg); f_vi != f_vend; ++f_vi) {
    std::pair<Graph::edge_descriptor, bool> edgePair = edge(sv, *f_vi, fg);
    Graph::edge_descriptor edge = edgePair.first;

    DEBUG("history.find", "u commodity = " << get(namemap, sv)->symbol());
    DEBUG("history.find", "v commodity = " << get(namemap, *f_vi)->symbol());

    const price_point_t& point(get(pricemap, edge));

    if (price.is_null() || point.when > most_recent) {
      most_recent = point.when;
      price       = point.price;
    }

    DEBUG("history.find", "price was = " << price.unrounded());

    if (price.commodity() == source) {
      price.in_place_invert();
      if (source == *get(namemap, sv))
        price.set_commodity(const_cast<commodity_t&>(*get(namemap, *f_vi)));
      else
        price.set_commodity(const_cast<commodity_t&>(*get(namemap, sv)));
    }

    DEBUG("history.find", "price is  = " << price.unrounded());
  }

  if (price.is_null()) {
    DEBUG("history.find", "there is no final price");
    return none;
  } else {
    DEBUG("history.find", "final price is = " << price.unrounded());
    return price_point_t(most_recent, price);
  }
}

optional<price_point_t>
commodity_history_t::find_price(const commodity_t& source,
                                const commodity_t& target,
                                const datetime_t&  moment,
                                const datetime_t&  oldest)
{
  vertex_descriptor sv = vertex(*source.graph_index(), price_graph);
  vertex_descriptor tv = vertex(*target.graph_index(), price_graph);

  FGraph fg(price_graph,
            recent_edge_weight<EdgeWeightMap, PricePointMap, PriceRatioMap>
            (get(edge_weight, price_graph), pricemap, ratiomap,
             moment, oldest));

  FNameMap namemap(get(vertex_name, fg));

  DEBUG("history.find", "sv commodity = " << get(namemap, sv)->symbol());
  DEBUG("history.find", "tv commodity = " << get(namemap, tv)->symbol());

  std::size_t                    vector_len(num_vertices(fg));
  std::vector<vertex_descriptor> predecessors(vector_len);
  std::vector<long>              distances(vector_len);

  FPredecessorMap predecessorMap(&predecessors[0]);
  FDistanceMap    distanceMap(&distances[0]);

  dijkstra_shortest_paths(fg, /* start= */ sv,
                          predecessor_map(predecessorMap)
                          .distance_map(distanceMap)
                          .distance_combine(f_max<long>()));

  // Extract the shortest path and performance the calculations
  datetime_t least_recent = moment;
  amount_t   price;

  const commodity_t * last_target = &target;

#if defined(REVERSE_PREDECESSOR_MAP)
  typedef tuple<const commodity_t *, const commodity_t *,
                const price_point_t *> results_tuple;
  std::vector<results_tuple> results;
  bool results_reversed = false;
#endif

  vertex_descriptor v = tv;
  for (vertex_descriptor u = predecessorMap[v]; 
       u != v;
       v = u, u = predecessorMap[v])
  {
    std::pair<Graph::edge_descriptor, bool> edgePair_uv = edge(u, v, fg);
    std::pair<Graph::edge_descriptor, bool> edgePair_vu = edge(v, u, fg);

    Graph::edge_descriptor edge_uv = edgePair_uv.first;
    Graph::edge_descriptor edge_vu = edgePair_vu.first;

    const price_point_t& point_uv(get(pricemap, edge_uv));
    const price_point_t& point_vu(get(pricemap, edge_vu));

    const price_point_t& point(point_vu.when > point_uv.when ?
                               point_vu : point_uv);

    const commodity_t * u_comm = get(namemap, u);
    const commodity_t * v_comm = get(namemap, v);

#if defined(REVERSE_PREDECESSOR_MAP)
    if (v == tv && u_comm != last_target && v_comm != last_target)
      results_reversed = true;

    results.push_back(results_tuple(u_comm, v_comm, &point));
  }

  if (results_reversed)
    std::reverse(results.begin(), results.end());

  foreach (const results_tuple& edge, results) {
    const commodity_t *  u_comm = edge.get<0>();
    const commodity_t *  v_comm = edge.get<1>();
    const price_point_t& point(*edge.get<2>());
#else
    assert(u_comm == last_target || v_comm == last_target);
#endif

    bool first_run = false;
    if (price.is_null()) {
      least_recent = point.when;
      first_run    = true;
    }
    else if (point.when < least_recent) {
      least_recent = point.when;
    }

    DEBUG("history.find", "u commodity = " << u_comm->symbol());
    DEBUG("history.find", "v commodity = " << v_comm->symbol());
    DEBUG("history.find", "last target = " << last_target->symbol());

    // Determine which direction we are converting in
    amount_t pprice(point.price);
    DEBUG("history.find", "pprice    = " << pprice.unrounded());

    if (! first_run) {
      DEBUG("history.find", "price was = " << price.unrounded());
      if (pprice.commodity_ptr() != last_target)
        price *= pprice.inverted();
      else
        price *= pprice;
    }
    else if (pprice.commodity_ptr() != last_target) {
      price = pprice.inverted();
    }
    else {
      price = pprice;
    }
    DEBUG("history.find", "price is  = " << price.unrounded());

    if (last_target == v_comm)
      last_target = u_comm;
    else
      last_target = v_comm;

    DEBUG("history.find", "last target now = " << last_target->symbol());
  }

  if (price.is_null()) {
    DEBUG("history.find", "there is no final price");
    return none;
  } else {
    price.set_commodity(const_cast<commodity_t&>(target));
    DEBUG("history.find", "final price is = " << price.unrounded());

    return price_point_t(least_recent, price);
  }
}

template <class Name>
class label_writer {
public:
  label_writer(Name _name) : name(_name) {}

  template <class VertexOrEdge>
  void operator()(std::ostream& out, const VertexOrEdge& v) const {
    out << "[label=\"" << name[v]->symbol() << "\"]";
  }

private:
  Name name;
};

void commodity_history_t::print_map(std::ostream& out, const datetime_t& moment)
{
  if (moment.is_not_a_date_time()) {
    write_graphviz(out, price_graph,
                   label_writer<NameMap>(get(vertex_name, price_graph)));
  } else {
    FGraph fg(price_graph,
              recent_edge_weight<EdgeWeightMap, PricePointMap, PriceRatioMap>
              (get(edge_weight, price_graph), pricemap, ratiomap, moment));
    write_graphviz(out, fg, label_writer<FNameMap>(get(vertex_name, fg)));
  }
}

} // namespace ledger