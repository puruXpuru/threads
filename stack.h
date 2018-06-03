#ifndef __stack_h
#define __stack_h

#include <atomic>
#include "hazard_pointer.h"

namespace threads{

	template<typename T>
	class stack{
		struct stack_node{
			T t;
			stack_node* next;
		};
		using stack_node = stack::stack_node;

		hazard_pointer __hp;
		std::atomic<int> __size;
		std::atomic<stack_node*> __head;
		
	public:

		stack(){
			__size = 0;
			__head = nullptr;
		}

		~stack(){
			auto hn = __head.load();
			decltype(hn) temp;
			do{
				if(!hn)
					break;
				temp = hn->next;
				if(__head.compare_exchange_strong(hn, hn->next)){
					if(!hn)
						break;
					delete hn;
					hn = temp;
				}
			}while(true);
		}


		void push(const T& t){
			auto n = new stack_node();
			n->t = t;
			n->next = __head.load();
			while(!__head.compare_exchange_strong(n->next, n));
			++__size;
		}

		bool pop(T& t){
			auto hazard_node = __hp.reference();
			stack_node* hn = __head.load();
			do{
				hazard_node->set(hn);
				if(!hn){
					hazard_node->dereference();
					return false;
				}
			}while(!__head.compare_exchange_strong(hn, hn->next));
			hazard_node->dereference();
			t = hn->t;
			__hp.delete_ptr(hn);
			--__size;
			return true;
		}

		//inline size_t threshold(){return __hp.threshold();}

		inline size_t size(){return __size;}
	};
	
}

#endif
