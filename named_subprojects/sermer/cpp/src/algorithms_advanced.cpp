#include "pyatnashki/types.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <random>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace pyatnashki {
namespace {
using Clock = std::chrono::steady_clock;
constexpr Cost INF_ADV = std::numeric_limits<Cost>::max();
Cost checked_add(Cost a, Cost b) {
    if (a > INF_ADV - b) throw std::runtime_error("cost overflow");
    return a + b;
}
Cost checked_multiply(Cost cost, int distance) {
    if (distance && cost > INF_ADV / distance) throw std::runtime_error("heuristic cost overflow");
    return cost * distance;
}
struct Node { Board b; int blank; Cost g; std::size_t parent; Move move; };
struct Rec { Cost g; std::size_t node; };
struct Edge { Board b; int blank; int tile; char dir; Cost cost; };

Cost manhattan(const Board& b, const Puzzle& p) {
    Cost h = 0;
    for (std::size_t i=0;i<b.size();++i) {
        const int t=b[i]; if (!t) continue;
        const int goal=t-1, dr=static_cast<int>(i)/p.cols-goal/p.cols, dc=static_cast<int>(i)%p.cols-goal%p.cols;
        h = checked_add(h, checked_multiply(dr > 0 ? p.costs[0] : p.costs[1], std::abs(dr)));
        h = checked_add(h, checked_multiply(dc > 0 ? p.costs[2] : p.costs[3], std::abs(dc)));
    }
    return h;
}
Cost linear_conflict(const Board& b, const Puzzle& p) {
    Cost h=manhattan(b,p);
    // Add only vertex-disjoint conflicts, preserving admissibility.
    for (int axis=0;axis<2;++axis) {
        const int lines=axis==0?p.rows:p.cols, width=axis==0?p.cols:p.rows;
        for(int line=0;line<lines;++line){
            std::vector<int> tiles;
            for(int k=0;k<width;++k){ int idx=axis==0?line*p.cols+k:k*p.cols+line; int t=b[idx]; if(!t)continue;
                int goal=t-1; int goal_line=axis==0?goal/p.cols:goal%p.cols;
                if(goal_line==line) tiles.push_back(t);
            }
            std::vector<bool> used(tiles.size(),false);
            for(std::size_t i=0;i<tiles.size();++i) if(!used[i]) {
                const int gi=axis==0?(tiles[i]-1)%p.cols:(tiles[i]-1)/p.cols;
                for(std::size_t j=i+1;j<tiles.size();++j) if(!used[j]) {
                    const int gj=axis==0?(tiles[j]-1)%p.cols:(tiles[j]-1)/p.cols;
                    if(gi>gj){
                        used[i]=used[j]=true;
                        h=checked_add(h, axis==0 ? checked_add(p.costs[0],p.costs[1])
                                                 : checked_add(p.costs[2],p.costs[3]));
                        break;
                    }
                }
            }
        }
    }
    return h;
}
std::vector<Edge> edges(const Board& b,int blank,const Puzzle& p) {
    const int r=blank/p.cols,col=blank%p.cols;
    std::vector<std::pair<int,char>> cand;
    if(r+1<p.rows) cand.push_back({blank+p.cols,'U'});
    if(r>0) cand.push_back({blank-p.cols,'D'});
    if(col+1<p.cols) cand.push_back({blank+1,'L'});
    if(col>0) cand.push_back({blank-1,'R'});
    std::vector<Edge> out; out.reserve(cand.size());
    for(auto [idx,d]:cand){ Board n=b; int tile=n[idx]; std::swap(n[idx],n[blank]); out.push_back({std::move(n),idx,tile,d,p.costs[static_cast<std::size_t>(direction_index(d))]}); }
    return out;
}
std::vector<Move> path_of(const std::vector<Node>& ns,std::size_t id){ std::vector<Move> p; while(ns[id].parent!=std::numeric_limits<std::size_t>::max()){p.push_back(ns[id].move);id=ns[id].parent;} std::reverse(p.begin(),p.end()); return p; }
}

SearchResult linear_conflict_astar(const Puzzle& p,std::uint64_t limit) {
    const auto started=Clock::now(); const Board goal=p.goal();
    struct Q { Cost f,g; std::uint64_t seq; std::size_t id; bool operator>(const Q&o)const{return f!=o.f?f>o.f:(g!=o.g?g<o.g:seq>o.seq);} };
    int blank=static_cast<int>(std::find(p.start.begin(),p.start.end(),0)-p.start.begin());
    std::vector<Node> ns{{p.start,blank,0,std::numeric_limits<std::size_t>::max(),{0,' '}}};
    std::priority_queue<Q,std::vector<Q>,std::greater<Q>> q; std::uint64_t seq=0,expanded=0;
    q.push({linear_conflict(p.start,p),0,seq++,0}); std::unordered_map<Board,Rec,BoardHash> best;best.emplace(p.start,Rec{0,0});
    while(!q.empty()) {auto cur=q.top();q.pop();const Node n=ns[cur.id];auto it=best.find(n.b);if(it==best.end()||it->second.node!=cur.id)continue;
        if(n.b==goal)return {path_of(ns,cur.id),n.g,expanded,std::chrono::duration<double>(Clock::now()-started).count()};
        if(++expanded>limit)throw std::runtime_error("search expansion limit exceeded");
        for(auto e:edges(n.b,n.blank,p)){if(n.g>INF_ADV-e.cost)throw std::runtime_error("path cost overflow");Cost ng=n.g+e.cost;auto old=best.find(e.b);if(old!=best.end()&&old->second.g<=ng)continue;
            Cost h=linear_conflict(e.b,p);if(ng>INF_ADV-h)throw std::runtime_error("priority overflow");auto id=ns.size();ns.push_back({std::move(e.b),e.blank,ng,cur.id,{e.tile,e.dir}});best[ns.back().b]={ng,id};q.push({ng+h,ng,seq++,id});}
    } throw std::runtime_error("no solution exists");
}

SearchResult rbfs(const Puzzle& p,std::uint64_t limit) {
    const auto started=Clock::now(); const Board goal=p.goal(); std::uint64_t expanded=0; std::vector<Move> path,answer; Cost answer_cost=0;
    struct Candidate { Board b; int blank; Cost g,f; Move move; };
    std::unordered_set<Board,BoardHash> onpath; onpath.insert(p.start);
    std::function<Cost(const Board&,int,Cost,Cost,Cost)> visit = [&](const Board& b,int blank,Cost g,Cost node_f,Cost f_limit)->Cost {
        if(b==goal){answer=path;answer_cost=g;return g;}
        if(++expanded>limit)throw std::runtime_error("search expansion limit exceeded");
        std::vector<Candidate> cs;
        for(auto e:edges(b,blank,p)){if(onpath.count(e.b))continue;Cost ng=checked_add(g,e.cost);Cost h=manhattan(e.b,p);Cost f=std::max(node_f,checked_add(ng,h));cs.push_back({std::move(e.b),e.blank,ng,f,{e.tile,e.dir}});}
        if(cs.empty())return INF_ADV;
        for(;;){std::sort(cs.begin(),cs.end(),[](const Candidate&a,const Candidate&b){return a.f<b.f;});auto &best=cs[0];if(best.f==INF_ADV || best.f>f_limit)return best.f;Cost alternative=cs.size()>1?cs[1].f:INF_ADV;
            onpath.insert(best.b);path.push_back(best.move);Cost backed=visit(best.b,best.blank,best.g,best.f,std::min(f_limit,alternative));path.pop_back();onpath.erase(best.b);best.f=backed;
            if(!answer.empty()||p.start==goal)return answer_cost;
        }
    };
    visit(p.start,static_cast<int>(std::find(p.start.begin(),p.start.end(),0)-p.start.begin()),0,manhattan(p.start,p),INF_ADV);
    if(answer.empty()&&p.start!=goal)throw std::runtime_error("RBFS failed to find a solution");
    return {answer,answer_cost,expanded,std::chrono::duration<double>(Clock::now()-started).count()};
}

SearchResult pattern_database(const Puzzle& p,std::uint64_t limit) {
    if(p.start.size()>9) throw std::runtime_error("PDB mode currently supports boards with at most 9 cells (3x3)");
    if(std::any_of(p.costs.begin(),p.costs.end(),[](Cost c){return c!=1;})) throw std::runtime_error("PDB mode currently requires unit move costs");
    // Complete pattern database for the whole small board, built lazily by reverse BFS.
    const auto started=Clock::now();Board goal=p.goal();std::unordered_map<Board,Cost,BoardHash> dist;std::queue<std::pair<Board,int>> q;
    int gb=static_cast<int>(goal.size()-1);dist.emplace(goal,0);q.push({goal,gb});std::uint64_t expanded=0;
    while(!q.empty()){if(++expanded>limit)throw std::runtime_error("search expansion limit exceeded while building PDB");auto [b,blank]=std::move(q.front());q.pop();Cost d=dist.at(b);for(auto e:edges(b,blank,p))if(!dist.count(e.b)){dist.emplace(e.b,d+1);q.push({std::move(e.b),e.blank});}}
    auto it=dist.find(p.start);if(it==dist.end())throw std::runtime_error("no solution exists");
    std::vector<Move> moves;Board cur=p.start;int blank=static_cast<int>(std::find(cur.begin(),cur.end(),0)-cur.begin());
    while(cur!=goal){bool moved=false;for(auto e:edges(cur,blank,p)){auto d=dist.find(e.b);if(d!=dist.end()&&d->second+1==dist.at(cur)){moves.push_back({e.tile,e.dir});cur=std::move(e.b);blank=e.blank;moved=true;break;}}if(!moved)throw std::runtime_error("PDB reconstruction failed");if(++expanded>limit)throw std::runtime_error("search expansion limit exceeded");}
    return {std::move(moves),static_cast<Cost>(dist.at(p.start)),expanded,std::chrono::duration<double>(Clock::now()-started).count()};
}

SearchResult simulated_annealing(const Puzzle& p,std::uint64_t limit) {
    const auto started=Clock::now();const Board goal=p.goal();if(p.start==goal)return {{},0,0,0};
    std::mt19937_64 rng(0x5A17EULL ^ static_cast<std::uint64_t>(p.start.size()));Board cur=p.start;int blank=static_cast<int>(std::find(cur.begin(),cur.end(),0)-cur.begin());
    std::vector<Move> curpath,bestpath;Board best=cur;Cost besth=manhattan(best,p);std::uint64_t expanded=0;const std::uint64_t budget=std::max<std::uint64_t>(limit,1);
    for(std::uint64_t i=0;i<budget;++i){auto es=edges(cur,blank,p);if(es.empty())break;std::uniform_int_distribution<std::size_t> pick(0,es.size()-1);auto e=std::move(es[pick(rng)]);Cost h=manhattan(e.b,p),oldh=manhattan(cur,p);double temp=std::max(0.02,20.0*(1.0-static_cast<double>(i)/budget));double delta=static_cast<double>(h-oldh);bool accept=delta<=0 || std::generate_canonical<double,53>(rng)<std::exp(-delta/temp);
        if(accept){cur=std::move(e.b);blank=e.blank;curpath.push_back({e.tile,e.dir});}else continue;
        ++expanded;if(h<besth){besth=h;best=cur;bestpath=curpath;}if(cur==goal){Cost cost=0;for(auto m:curpath)cost=checked_add(cost,p.costs[static_cast<std::size_t>(direction_index(m.direction))]);return {curpath,cost,expanded,std::chrono::duration<double>(Clock::now()-started).count()};}
        if(curpath.size()>500){cur=p.start;blank=static_cast<int>(std::find(cur.begin(),cur.end(),0)-cur.begin());curpath.clear();}
    }
    throw std::runtime_error("simulated annealing did not find a solution within the iteration budget; best Manhattan heuristic="+std::to_string(besth));
}
}
