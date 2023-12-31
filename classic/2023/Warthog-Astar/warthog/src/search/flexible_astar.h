#ifndef FLEXIBLE_ASTAR_H
#define FLEXIBLE_ASTAR_H

// flexible_astar.h
//
// A* implementation that allows arbitrary combinations of
// (weighted) heuristic functions and node expansion policies.
// This implementation uses a binary heap for the open_ list
// and a bit array for the closed_ list.
//
// TODO: is it better to store a separate closed list and ungenerate nodes
// or use more memory and not ungenerate until the end of search??
// 32bytes vs... whatever unordered_map overhead is a two integer key/value pair
//
// @author: dharabor
// @created: 21/08/2012
//

#include "cpool.h"
#include "search/dummy_listener.h"
#include "pqueue.h"
#include "problem_instance.h"
#include "search.h"
#include "search_node.h"
#include "solution.h"
#include "timer.h"

#include <functional>
#include <iostream>
#include <memory>
#include <vector>

namespace warthog
{

// H is a heuristic function
// E is an expansion policy
template< class H,
          class E,
          class Q = warthog::pqueue_min,
          class L = warthog::dummy_listener >
class flexible_astar: public warthog::search
{
	public:
		flexible_astar(H* heuristic, E* expander, Q* queue, L* listener = 0) :
            heuristic_(heuristic), expander_(expander), open_(queue),
            listener_(listener)
		{
            cost_cutoff_ = warthog::COST_MAX;
            exp_cutoff_ = UINT32_MAX;
		}

		virtual ~flexible_astar() { }

        virtual void
		get_pathcost(
                warthog::problem_instance& pi, warthog::solution& sol)
        {

            sol.reset();
			warthog::search_node* target_node = search(&pi, &sol);

			if(target_node)
			{
                sol.sum_of_edge_costs_ = target_node->get_g();
            }
        }

        virtual void
		get_path(warthog::problem_instance& pi, warthog::solution& sol)
		{
            sol.reset();

			warthog::search_node* target = search(&pi, &sol);
			if(target)
			{
                sol.sum_of_edge_costs_ = target->get_g();

				// follow backpointers to extract the path
				assert(expander_->is_target(target, &pi));
                warthog::search_node* current = target;
				while(true)
                {
                    sol.path_.push_back(current->get_id());
                    if(current->get_parent() == warthog::SN_ID_MAX) break;
                    current = expander_->generate(current->get_parent());
				}
                std::reverse(sol.path_.begin(), sol.path_.end());

                #ifndef NDEBUG
                if(pi.verbose_)
                {
                    for(auto& state : sol.path_)
                    {
                        int32_t x, y;
                        expander_->get_xy(state, x, y);
                        std::cerr
                            << "final path: (" << x << ", " << y << ")...";
                        warthog::search_node* n =
                            expander_->generate(state);
                        assert(n->get_search_number() == pi.instance_id_);
                        n->print(std::cerr);
                        std::cerr << std::endl;
                    }
                }
                #endif
            }
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

        void
        set_listener(L* listener)
        { listener_ = listener; }

		virtual inline size_t
		mem()
		{
			size_t bytes =
				// memory for the priority quete
				open_->mem() +
				// gridmap size and other stuff needed to expand nodes
				expander_->mem() +
                // heuristic uses some memory too
                heuristic_->mem() +
				// misc
				sizeof(*this);
			return bytes;
		}


	private:
		H* heuristic_;
		E* expander_;
		Q* open_;
        L* listener_;

        // early termination limits
        warthog::cost_t cost_cutoff_;
        uint32_t exp_cutoff_;

		// no copy ctor
		flexible_astar(const flexible_astar& other) { }
		flexible_astar&
		operator=(const flexible_astar& other) { return *this; }

		warthog::search_node*
		search(warthog::problem_instance* pi, warthog::solution* sol)
		{
			warthog::timer mytimer;
			mytimer.start();
			open_->clear();

			warthog::search_node* start;
			warthog::search_node* target = 0;

            // get the internal target id
            if(pi->target_ != warthog::SN_ID_MAX)
            {
                warthog::search_node* target =
                    expander_->generate_target_node(pi);
                if(!target) { return 0; } // invalid target location
                pi->target_ = target->get_id();

            }

            // initialise and push the start node
            if(pi->start_ == warthog::SN_ID_MAX) { return 0; }
            start = expander_->generate_start_node(pi);
            assert(start->get_search_number() != pi->instance_id_);
            if(!start) { return 0; } // invalid start location
            pi->start_ = start->get_id();

			start->init(pi->instance_id_, warthog::SN_ID_MAX,
                    0, heuristic_->h(pi->start_, pi->target_));

			open_->push(start);
            
            listener_->generate_node(0, start, 0, UINT32_MAX);

			#ifndef NDEBUG
			if(pi->verbose_) { pi->print(std::cerr); std:: cerr << "\n";}
			#endif

            // begin expanding
			while(open_->size())
			{
                // early termination: in case we want bounded-cost
                // search or if we want to impose some memory limit
                if(open_->peek()->get_f() > cost_cutoff_) { break; }
                if(sol->met_.nodes_expanded_ >= exp_cutoff_) { break; }

				warthog::search_node* current = open_->pop();
				current->set_expanded(true); // NB: set before generating
				assert(current->get_expanded());
				sol->met_.nodes_expanded_++;

                listener_->expand_node(current);

                // goal test
                if(expander_->is_target(current, pi))
                {
                    target = current;
                    break;
                }

				#ifndef NDEBUG
				if(pi->verbose_)
				{
					int32_t x, y;
                    expander_->get_xy(current->get_id(), x, y);
					std::cerr
                        << sol->met_.nodes_expanded_
                        << ". expanding ("<<x<<", "<<y<<")...";
					current->print(std::cerr);
					std::cerr << std::endl;
				}
				#endif

                // generate successors
				expander_->expand(current, pi);
				warthog::search_node* n = 0;
				warthog::cost_t cost_to_n = 0;
                uint32_t edge_id = 0;
				for(expander_->first(n, cost_to_n);
						n != 0;
					   	expander_->next(n, cost_to_n))
				{
                    listener_->generate_node(current, n, cost_to_n, edge_id++);
                    warthog::cost_t gval = current->get_g() + cost_to_n;
                    sol->met_.nodes_generated_++;
                    
                    if(n->get_search_number() != current->get_search_number())
                    {
                        // add new nodes to the fringe
                        n->init(current->get_search_number(), current->get_id(),
                            gval,
                            gval + heuristic_->h(n->get_id(),pi->target_));

                        open_->push(n);

                        #ifndef NDEBUG
                        if(pi->verbose_)
                        {
                            int32_t nx, ny;
                            expander_->get_xy(n->get_id(), nx, ny);
                            std::cerr
                                << "  generating (edgecost="
                                << cost_to_n<<") ("<< nx <<", "<< ny <<")...";
                            n->print(std::cerr);
                            std::cerr << std::endl;
                        }
                        #endif

                        listener_->relax_node(n);
                    }
                    else if(gval < n->get_g())
                    {
                        // relax already generated nodes
                        n->relax(gval, current->get_id());
                        if(n->get_expanded())
                        {
                            // reopen
                            n->set_expanded(false);
                            open_->push(n);
                            sol->met_.nodes_reopen_++;
                        }
                        else
                        {
                            // update priority
                            open_->decrease_key(n);
                        }

                        #ifndef NDEBUG
                        if(pi->verbose_)
                        {
                            int32_t x, y;
                            expander_->get_xy(n->get_id(), x, y);
                            std::cerr 
                                << "  updating (edgecost="
                                << cost_to_n<<") ("<<x<<", "<<y<<")...";
                            n->print(std::cerr);
                            std::cerr << std::endl;
                        }
                        #endif

                        listener_->relax_node(n);
                    }
                    else
                    {
                        #ifndef NDEBUG
                        if(pi->verbose_)
                        {
                            int32_t x, y;
                            expander_->get_xy(n->get_id(), x, y);
                            std::cerr 
                                << "  dominated (edgecost=" 
                                << cost_to_n<< ") ("<<x<<", "<<y<<")...";
                            n->print(std::cerr);
                            std::cerr << std::endl;
                        }
                        #endif
					}
				}
			}

			sol->met_.time_elapsed_nano_ = mytimer.elapsed_time_nano();
            sol->met_.nodes_surplus_ = open_->size();
            sol->met_.heap_ops_ = open_->get_heap_ops();

            #ifndef NDEBUG
            if(pi->verbose_)
            {
                if(target == 0)
                {
                    std::cerr
                        << "search failed; no solution exists " << std::endl;
                }
                else
                {
                    int32_t x, y;
                    expander_->get_xy(target->get_id(), x, y);
                    std::cerr << "target found ("<<x<<", "<<y<<")...";
                    target->print(std::cerr);
                    std::cerr << std::endl;
                }
            }
            #endif

            return target;
		}
};

}

#endif
