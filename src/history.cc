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

void commodity_history_t::add_commodity(const commodity_t& comm)
{
  const vertex_descriptor vert = add_vertex(&comm, price_graph);
  put(indexmap, vert, reinterpret_cast<std::size_t>(&comm));
}

void commodity_history_t::add_price(const commodity_t& source,
                                    const datetime_t&  when,
                                    const amount_t&    price)
{
  vertex_descriptor sv =
    vertex(reinterpret_cast<std::size_t>(&source), price_graph);
  vertex_descriptor tv =
    vertex(reinterpret_cast<std::size_t>(&price.commodity()), price_graph);

  std::pair<edge_descriptor, bool> e1 = add_edge(sv, tv, 0, price_graph);
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
  vertex_descriptor sv =
    vertex(reinterpret_cast<std::size_t>(&source), price_graph);
  vertex_descriptor tv =
    vertex(reinterpret_cast<std::size_t>(&target), price_graph);

  std::pair<edge_descriptor, bool> e1 = add_edge(sv, tv, 0, price_graph);
  price_map_t& prices(get(ratiomap, e1.first));

  // jww (2012-03-04): If it fails, should we give a warning?
  prices.erase(date);
}

optional<price_point_t>
commodity_history_t::find_price(const commodity_t&            source,
                                const datetime_t&             moment,
                                const optional<datetime_t>&   oldest,
                                const optional<commodity_t&>& target)
{
  vertex_descriptor sv =
    vertex(reinterpret_cast<std::size_t>(&source), price_graph);
  vertex_descriptor tv =
    vertex(reinterpret_cast<std::size_t>(&*target), price_graph);

  // Filter out edges which came into being after the reference time

  FGraph fg(price_graph,
            recent_edge_weight<EdgeWeightMap, PricePointMap, PriceRatioMap>
            (get(edge_weight, price_graph), pricemap, ratiomap,
             moment, oldest));
  
  std::vector<vertex_descriptor> predecessors(num_vertices(fg));
  std::vector<long>              distances(num_vertices(fg));
  
  PredecessorMap predecessorMap(&predecessors[0]);
  DistanceMap    distanceMap(&distances[0]);

  dijkstra_shortest_paths(fg, /* start= */ sv,
                          predecessor_map(predecessorMap)
                          .distance_map(distanceMap)
                          .distance_combine(f_max<long>()));

  // Extract the shortest path and performance the calculations
  datetime_t least_recent = moment;
  amount_t price;

  vertex_descriptor v = tv;
  for (vertex_descriptor u = predecessorMap[v]; 
       u != v;
       v = u, u = predecessorMap[v])
  {
    std::pair<Graph::edge_descriptor, bool> edgePair = edge(u, v, fg);
    Graph::edge_descriptor edge = edgePair.first;

    const price_point_t& point(get(pricemap, edge));

    if (price.is_null()) {
      least_recent = point.when;
      price        = point.price;
    }
    else if (point.when < least_recent)
      least_recent = point.when;

    // jww (2012-03-04): TODO
    //price *= point.price;
  }

  return price_point_t(least_recent, price);
}

#if 0
  print_vertices(fg, f_commmap);
  print_edges(fg, f_commmap);
  print_graph(fg, f_commmap);

  graph_traits<FGraph>::vertex_iterator f_vi, f_vend;
  for(tie(f_vi, f_vend) = vertices(fg); f_vi != f_vend; ++f_vi)
    std::cerr << get(f_commmap, *f_vi) << " is in the filtered graph"
              << std::endl;

  for (tie(f_vi, f_vend) = vertices(fg); f_vi != f_vend; ++f_vi) {
    std::cerr << "distance(" << get(f_commmap, *f_vi) << ") = "
              << distanceMap[*f_vi] << ", ";
    std::cerr << "parent(" << get(f_commmap, *f_vi) << ") = "
              << get(f_commmap, predecessorMap[*f_vi])
              << std::endl;
  }

  // Write shortest path
  FCommMap f_commmap = get(vertex_comm, fg);

  std::cerr << "Shortest path from CAD to EUR:" << std::endl;
  for (PathType::reverse_iterator pathIterator = path.rbegin();
       pathIterator != path.rend();
       ++pathIterator)
  {
    std::cerr << get(f_commmap, source(*pathIterator, fg))
              << " -> " << get(f_commmap, target(*pathIterator, fg))
              << " = " << get(edge_weight, fg, *pathIterator)
              << std::endl;
  }
  std::cerr << std::endl;

  std::cerr << "Distance: " << distanceMap[vd4] << std::endl;
#endif

#if 0
  #include <boost/graph/graphviz.hpp>

  // Writing graph to file
  {
    std::ofstream f("test.dot");

    dynamic_properties p;
    p.property("label", get(edge_weight, g));
    p.property("weight", get(edge_weight, g));
    p.property("node_id", get(vertex_comm, g));
    write_graphviz(f,g,p);
    f.close();
  }
#endif

} // namespace ledger
