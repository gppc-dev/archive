#ifndef WARTHOG_BIDIRECTIONAL_SEARCH_H
#define WARTHOG_BIDIRECTIONAL_SEARCH_H

// bidirectional_search.h
//
// A customisable variant of bidirectional best-first search.
// Users can pass in any heuristic and any (domain-specific) expansion policy.
//
// @author: dharabor
// @created: 2016-02-14
//

#include "bds_traits.h"
#include "constants.h"
#include "graph_expansion_policy.h"
#include "xy_graph.h"
#include "pqueue.h"
#include "search.h"
#include "search_node.h"
#include "solution.h"
#include "timer.h"
#include "zero_heuristic.h"

#include <cstdlib>
#include <stack>
#include <cstdint>
#include <typeinfo>

namespace warthog
{


template<class H, class E, class SEARCH_TRAITS>
class bidirectional_search  : public warthog::search
{
    public:
        bidirectional_search(E* fexp, E* bexp, H* heuristic)
            : fexpander_(fexp), bexpander_(bexp), heuristic_(heuristic)
        {
            fopen_ = new pqueue_min(512);
            bopen_ = new pqueue_min(512);
            
            //dijkstra_ = false;
            //if(typeid(*heuristic_) == typeid(warthog::zero_heuristic))
            //{
            //    dijkstra_ = true;
            //}

            exp_cutoff_ = warthog::INF32;
            cost_cutoff_ = warthog::COST_MAX;
        }

        ~bidirectional_search()
        {
            delete fopen_;
            delete bopen_;
        }

        virtual void
        get_path(warthog::problem_instance& pi, warthog::solution& sol)
        {
            __get_path(pi, sol, false);

        }

        void
        __get_path(warthog::problem_instance& pi, warthog::solution& sol, bool resume=false)
        {
            this->search(&pi, &sol, resume);
            if(best_cost_ != warthog::COST_MAX)
            { 
                sol.sum_of_edge_costs_ = best_cost_;
                reconstruct_path(&pi, &sol);
            }
        }
        
        virtual void
        get_pathcost(warthog::problem_instance& pi, warthog::solution& sol)
        {
            __get_pathcost(pi, sol, false);
        }

        void
        __get_pathcost(warthog::problem_instance& pi, warthog::solution& sol, bool resume=false)
        {
            this->search(&pi, &sol, resume);
            assert(sol.met_.nodes_expanded_ <= exp_cutoff_);

            if(best_cost_ != warthog::COST_MAX)
            { sol.sum_of_edge_costs_ = best_cost_; }
        }
        
        // set a cost-cutoff to run a bounded-cost A* search.
        // the search terminates when the target is found or the f-cost 
        // limit is reached.
        inline void
        set_cost_cutoff(warthog::cost_t cutoff) { cost_cutoff_ = cutoff; }

        inline warthog::cost_t
        get_cost_cutoff() { return cost_cutoff_; }

        // set a cutoff on the maximum number of node expansions.
        // the search terminates when the target is found or when
        // the limit is reached
        inline void
        set_max_expansions_cutoff(uint32_t cutoff) { exp_cutoff_ = cutoff; }

        inline uint32_t 
        get_max_expansions_cutoff() { return exp_cutoff_; } 

        inline void
        set_time_cutoff(uint64_t nanos)
        {
            time_cutoff_nanos_ = nanos;
        }

        uint64_t
        get_time_cutoff() 
        {
            return time_cutoff_nanos_;
        }

        virtual size_t
        mem()
        {
            return sizeof(*this) + 
                fopen_->mem() +
                bopen_->mem() +
                fexpander_->mem();
                bexpander_->mem();
        }

    private:
        warthog::pqueue_min* fopen_;
        warthog::pqueue_min* bopen_;
        E* fexpander_;
        E* bexpander_;
        H* heuristic_;

        // early termination limits
        warthog::cost_t cost_cutoff_; 
        uint32_t exp_cutoff_;
        uint64_t time_cutoff_nanos_;

        // v is the section of the path in the forward
        // direction and w is the section of the path
        // in the backward direction. need parent pointers
        // of both to extract the actual path
        warthog::search_node* v_;
        warthog::search_node* w_;
        warthog::cost_t best_cost_;

        void
        reconstruct_path(warthog::problem_instance* pi, warthog::solution* sol)
        {
            if(v_ && (&*v_ == &*bexpander_->generate(v_->get_id())))
            {
                warthog::search_node* tmp = v_;
                v_ = w_;
                w_ = tmp;
            }

            warthog::search_node* current = v_;
            while(current)
            {
               sol->path_.push_back(current->get_id());
               current = current->get_parent() == warthog::NO_PARENT ? 0 : 
                   fexpander_->generate(current->get_parent());
            }
            std::reverse(sol->path_.begin(), sol->path_.end());

            #ifndef NDEBUG
            if(pi->verbose_)
            {
                for(auto& state : sol->path_)
                {
                    int32_t x, y;
                    fexpander_->get_xy(state, x, y);
                    std::cerr 
                        << "(f) final path: (" << x << ", " << y << ")...";
                    warthog::search_node* n = 
                        fexpander_->generate(state);
                    n->print(std::cerr);
                    std::cerr << std::endl;
                }
            }
            #endif

            // already added the meeting point node
            current = 0;
            if(w_ && w_->get_parent() != warthog::NO_PARENT)
            {
                current = bexpander_->generate(w_->get_parent());
            }

            while(current)
            {  
               #ifndef NDEBUG
                if(pi->verbose_)
                {
                    int32_t x, y;
                    bexpander_->get_xy(current->get_id(), x, y);
                    std::cerr 
                        << "(b) final path: (" << x << ", " << y << ")...";
                    current->print(std::cerr);
                    std::cerr << std::endl;
                }
               #endif

               sol->path_.push_back(current->get_id());
               current = current->get_parent() == warthog::NO_PARENT ? 0 : 
                   bexpander_->generate(current->get_parent());
            }
        }

        // modify this function to balance the search
        // by default the search expands one node in
        // each direction then switches to the other direction
        bool
        forward_next()
        {
            warthog::cost_t fwd_min, bwd_min;
            bwd_min = bopen_->size() ? bopen_->peek()->get_f() : warthog::COST_MAX;
            fwd_min = fopen_->size() ? fopen_->peek()->get_f() : warthog::COST_MAX;
            return fwd_min <= bwd_min;
        }

        void 
        search(warthog::problem_instance* pi, warthog::solution* sol, bool resume)
        {
            warthog::timer mytimer;
            mytimer.start();

            #ifndef NDEBUG
            if(pi->verbose_)
            {
                std::cerr << "bidirectional_search. ";
                pi->print(std::cerr);
                std::cerr << std::endl;
            }
            #endif

            best_cost_ = warthog::COST_MAX;
            v_ = 0;
            w_ = 0;
            sol->reset();
            uint32_t fwd_instance_id = pi->instance_id_;
            uint32_t bwd_instance_id = pi->instance_id_;

            // check for valid start and target
            if(!bexpander_->generate_start_node(pi)  ||
               !bexpander_->generate_target_node(pi) ||
               !fexpander_->generate_start_node(pi)  ||
               !fexpander_->generate_target_node(pi))
            { return; } 
            
            // initialise the backward search
            { 
                bexpander_->generate_target_node(pi)->init(
                    bwd_instance_id, warthog::NO_PARENT, 0,
                    heuristic_->h(
                        bexpander_->generate_target_node(pi)->get_id(),
                        bexpander_->generate_start_node(pi)->get_id()));
                bopen_->clear();
                bopen_->push(bexpander_->generate_target_node(pi));

            }

            // initialise or resume the forward search. this is useful if
            // the start node is the same across multiple instances.
            // but resuming only works if the heuristic function is admissible
            // for the new target as well as the old. Only multi-target algorithms, 
            // like Dijkstra Search, have this property. 
            if(resume)
            { 
                warthog::search_node* fwd_start = 
                    fexpander_->generate_start_node(pi);
                warthog::search_node* fwd_target = 
                    fexpander_->generate_target_node(pi);
                if( fwd_target->get_search_number() == 
                     fwd_start->get_search_number() )
                {
                    best_cost_ = fwd_target->get_g();
                    v_ = fwd_target;
                    w_ = 0;
                }
                // **IMPORTANT** re-use the old instance_id
                fwd_instance_id = fwd_start->get_search_number();
            }
            else
            {
                fexpander_->generate_start_node(pi)->init(
                    fwd_instance_id, warthog::NO_PARENT, 0,
                    heuristic_->h(
                        fexpander_->generate_start_node(pi)->get_id(), 
                        fexpander_->generate_target_node(pi)->get_id()));
                fopen_->clear();
                fopen_->push(fexpander_->generate_start_node(pi));
            }


            // main expansion loop 
            // (breaks when it can be proven the problem is not solvable)
            while(SEARCH_TRAITS::solvable(fopen_->size(), bopen_->size()))
            {
                // get the current lower bound
                warthog::cost_t best_bound = 
                    SEARCH_TRAITS::lowerbound(fopen_->peek(), bopen_->peek());

                // check if we can terminate 
                if(best_bound >= best_cost_ ||
                   best_bound > cost_cutoff_ || 
                   sol->met_.nodes_expanded_ > exp_cutoff_ ||
                   (time_cutoff_nanos_ > mytimer.elapsed_time_nano() &&
                       best_cost_ <= cost_cutoff_))
                { 
                    break; 
                }

                // keep expanding; most promising node in either direction
                if(forward_next())
                {
                    warthog::search_node* current = fopen_->pop();
                    expand(current, fopen_, fexpander_, bexpander_, 
                            pi, sol, pi->target_, v_, w_, 
                            fwd_instance_id, bwd_instance_id);
                }
                else 
                {
                    warthog::search_node* current = bopen_->pop();
                    expand(current, bopen_, bexpander_, fexpander_, 
                            pi, sol, pi->start_, w_, v_, 
                            bwd_instance_id, fwd_instance_id);
                }
            }

            // don't return solutions that exceed the cost limit
            if(best_cost_ > cost_cutoff_)
            {
                v_ = 0;
                w_ = 0;
                best_cost_ = warthog::COST_MAX;
            }

			sol->met_.time_elapsed_nano_ = mytimer.elapsed_time_nano();
            sol->met_.nodes_surplus_ = fopen_->size() + bopen_->size();
            sol->met_.heap_ops_ = fopen_->get_heap_ops() + bopen_->get_heap_ops();
        }

        void
        expand( warthog::search_node* current, 
                warthog::pqueue_min* open, E* expander, E* reverse_expander, 
                warthog::problem_instance* pi, warthog::solution* sol,
                warthog::sn_id_t tmp_targetid, 
                warthog::search_node*& fwd_meet, warthog::search_node*& bwd_meet,
                uint32_t fwd_instance_id, uint32_t bwd_instance_id)
        {
            if(current == 0) { return; }
            current->set_expanded(true);
            expander->expand(current, pi);
            sol->met_.nodes_expanded_++;

            #ifndef NDEBUG
            if(pi->verbose_)
            {
                int32_t x, y;
                expander->get_xy(current->get_id(), x, y);
                std::cerr 
                    << sol->met_.nodes_expanded_ 
                    << ". expanding " 
                    << (pi->target_ == tmp_targetid ? "(f)" : "(b)")
                    << " ("<<x<<", "<<y<<")...";
                current->print(std::cerr);
                std::cerr << std::endl;
            }
            #endif
            
            // generate all neighbours
            warthog::search_node* n = 0;
            warthog::cost_t cost_to_n = warthog::COST_MAX;
            for(expander->first(n, cost_to_n); 
                    n != 0; 
                    expander->next(n, cost_to_n))
            {
                sol->met_.nodes_generated_++;
                warthog::cost_t gval = current->get_g() + cost_to_n;

                if(n->get_search_number() != current->get_search_number())
                {
                    // add new nodes to the fringe
                    n->init(current->get_search_number(), current->get_id(), 
                            gval,
                            gval + heuristic_->h(n->get_id(), tmp_targetid));
                    open->push(n);
                    #ifndef NDEBUG
                    if(pi->verbose_)
                    {
                        int32_t x, y;
                        expander->get_xy(n->get_id(), x, y);
                        std::cerr << "  generating "
                            << "(edgecost=" << cost_to_n<<") " 
                            << "("<<x<<", "<<y<<")...";
                        n->print(std::cerr);
                        std::cerr << std::endl;
                    }
                    #endif
                }
                else if(gval < n->get_g())
                {
                    // relax previously generated nodes
                    n->relax(gval, current->get_id());
                    if(n->get_expanded())
                    {
                        n->set_expanded(false);
                        open->push(n);
                    }
                    else
                    {
                        open->decrease_key(n);
                        assert(open->contains(n));
                    }

                    #ifndef NDEBUG
                    if(pi->verbose_)
                    {
                        int32_t x, y;
                        expander->get_xy(n->get_id(), x, y);
                        std::cerr << "  updating "
                            << "(edgecost="<< cost_to_n<<") "
                            << "("<<x<<", "<<y<<")...";
                        n->print(std::cerr);
                        std::cerr << std::endl;
                    }
                    #endif
                }
                else
                {
                    #ifndef NDEBUG
                    if(pi->verbose_)
                    {
                        int32_t x, y;
                        expander->get_xy(n->get_id(), x, y);
                        std::cerr << "  dominated "
                            << "(edgecost=" << cost_to_n<< ") "
                            << "("<<x<<", "<<y<<")...";
                        n->print(std::cerr);
                        std::cerr << std::endl;
                    }
                    #endif
                    continue;
                }
               
                // update the best solution if possible
                warthog::search_node* rev_n = 
                    reverse_expander->generate(n->get_id());
                if(rev_n->get_search_number() == bwd_instance_id)
                {
                    if((n->get_g() + rev_n->get_g()) < best_cost_)
                    {
                        fwd_meet = n;
                        bwd_meet = rev_n;
                        best_cost_ = n->get_g() + rev_n->get_g();

                        #ifndef NDEBUG
                        if(pi->verbose_)
                        {
                            int32_t x, y;
                            expander->get_xy(n->get_id(), x, y);
                            std::cerr <<"new best solution!  cost=" << best_cost_<<std::endl;
                        }
                        #endif
                    }
                }
            }
        }

        // clear the open lists and return all memory allocated for nodes
        // to the node pool
        void
        reclaim()
        {
            fopen_->clear();
            bopen_->clear();
            fexpander_->reclaim();
            bexpander_->reclaim();
        }
};

template<class H, class E> using bhs_search = bidirectional_search<H, E, warthog::bds_traits<BDS_ALGO::BHS>>;
template<class H, class E> using bdijk_search = bidirectional_search<H, E, warthog::bds_traits<BDS_ALGO::BDIJK>>;
template<class H, class E> using bch_search = bidirectional_search<H, E, warthog::bds_traits<BDS_ALGO::BCH>>;

}

#endif

