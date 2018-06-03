#ifndef __queue_h
#define __queue_h

#include <atomic>
#include "hazard_pointer.h"

namespace threads{
	template<typename T>
	class queue{
		struct queue_node{
			T t;
			std::atomic<queue_node*> next{nullptr};
		};

		using queue_node = queue::queue_node;
		
		hazard_pointer __hp;
		std::atomic<int> __size;
		std::atomic<queue_node*> __head, __tail;
		//hazard_node* __tail_keeper;
		

	public:
		queue(){
			__size = 0;
			auto dummy_node = new queue_node;
			__head = dummy_node;
			__tail = dummy_node;
			//__tail_keeper = __hp.reference();
		}

		~queue(){
			auto hpn = __hp.reference();
			do{
				auto hn = __head.load();
				if(!hn)
					break;
				hpn->set(hn);
				if(__head.compare_exchange_strong(hn, 
							hn->next.load()))
					delete hn;
				
			}while(true);
			hpn->dereference();
		}

		void push(const T& t){
			auto hpn = __hp.reference();
			auto n = new queue_node;
			n->t = t;
			queue_node* tn, *temp;
			do{
				temp = nullptr;
				tn = __tail.load();
				hpn->set(tn);
			}while(!tn->next.compare_exchange_strong(temp, n));
			hpn->dereference();
			__tail.store(n);
			++__size;
			return;
		}

		bool pop(T& t){
			auto hpn = __hp.reference();
			queue_node *hn, *hnn;
			do{
				hn = __head.load();
				hpn->set(hn);
				hnn = hn->next.load();
				if(hnn == nullptr){
					hpn->dereference();
					return false;
				}
				hpn->set(hnn);
			}while(!__head.compare_exchange_strong(hn, hnn));
			t = hnn->t;
			hpn->dereference();
			__hp.delete_ptr(hn);
			--__size;
			return true;
		}

		bool contains(const T& t){
			auto hpn = __hp.reference();
			auto hn = __head.load();
			bool rst  = false;
			do{
				if(!hn)
					break;
				hpn->set(hn);
				if(hn->t == t){
					rst = true;
					break;
				}
				hn = hn->next.load();
			}while(true);
			hpn->dereference();
			return rst;
		}

		inline size_t size(){return __size;}

		// inline size_t threshold(){return __hp.threshold();}

		inline bool empty(){return __head.load()->next == nullptr;}
	};
}


#endif
