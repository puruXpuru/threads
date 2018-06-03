/* References:
 * Lock Free Data Structures with Hazard Pointers
 * 				by Andrei Alexandrescu & Maged Michael
 * 
 * C++ Concurrency in Action: Practical Multithreading
 * 				by Anthony Williams
 */ 

#ifndef __hazard_pointer_h
#define __hazard_pointer_h

#include <atomic>
#include <unordered_set>
#include <unordered_map>
#include <condition_variable>

namespace threads{

class hazard_pointer;
class hazard_node{
	friend class hazard_pointer;
	hazard_node* next; 
	std::atomic<bool> occupied;
	std::atomic<void*> hazard_ptr; 
public:
	inline void set(void* ptr){
		hazard_ptr = ptr;
	}

	inline void dereference(){
		hazard_ptr = nullptr;
		occupied = false;
	}

};
class hazard_pointer{
	struct hazard_pending_node{
		void* ptr;
		hazard_pending_node* next;
		void(*deleter)(void*);
	};

	using hazard_pending_node = hazard_pointer::hazard_pending_node;

	// size of hazard_nodes this class is holding.
	std::atomic<size_t> __size;
	std::atomic<hazard_node*>  __head;
	std::atomic<size_t> __pending_size;

	/*Clean up the __pending_list as the __pending_size
	 * reaches this threshold.*/
	std::atomic<size_t> __threshold_of_clean_up;

	// nodes waiting for to be deleted.
	std::atomic<hazard_pending_node*> __pending_list;

	template<typename T>
	static inline void delete_ptr_final(void* ptr){
		delete (T*)ptr;
	}

	inline hazard_node* head(){
		return __head.load();
	}

	void check_threshold(){
		auto psize = pending_size();
		do{
			if(psize < __threshold_of_clean_up)
				return;
		}while(!__pending_size.compare_exchange_strong(psize, 0));

		auto list = __pending_list.exchange(nullptr);
		if(list == nullptr)
			return;
		__pending_size = 0;

		auto hn = head();
		std::unordered_map<void*, bool> filter;
		std::unordered_map<void*, hazard_pending_node*> new_list_filter;

		for(;hn;hn = hn->next){ 
			/* insert all the hazardous pointers into filter
			 * with a false flag to indicate that 
			 * it is a hazardout ptr. */
			if(hn->hazard_ptr == nullptr)
				continue;
			filter[hn->hazard_ptr] = false;
		}
		decltype(list) temp;
		decltype(list) new_list = nullptr;
		do{
			/* check if the pointers into __pending_list 
			 * can be deleted or not. */
			temp = list->next;
			auto it = filter.find(list->ptr);
			if(it == filter.end()){
				/* This ptr can be deleted safely.
				 * set it with true flag, indicate that it is 
				 * deleted.*/
				filter[list->ptr] = true; 
				list->deleter(list->ptr);
				delete list;
			}
			else if(!it->second){ 
				/* the ptr is a hazardous ptr, add it to the 
				 * new list for later deletion.
				 * Never insert a ptr twice, otherwise
				 * it still has a chance to fall, 
				 * so using a map to filter the duplicated.
				 * */
				auto nit = new_list_filter.insert(
						std::make_pair(list->ptr, list));
				if(!nit.second)
					delete list;
			}
			else{ 
				// this ptr was deleted.  
				delete list; 
			}
			list = temp;
			
		}while(list);

		/* Add the nodes that can not be deleted back into 
		 * the __pending_list. */
		if(new_list_filter.size() > 0){
			for(auto& it: new_list_filter){
				it.second->next = new_list;
				new_list = it.second;
			}
				
			decltype(new_list) pn = nullptr;
			// try to exchange the whole list back directlly.
			if(__pending_list
				.compare_exchange_strong(pn, new_list))
				return ; // done luckly.
			
			// add them back one by one.
			for(pn = new_list;pn;){
				temp = pn->next;
				pn->next = __pending_list.load();
				while(!__pending_list
					.compare_exchange_strong(pn->next, pn));
				++__pending_size;
				pn = temp;
			}
		}
	}

	/* will invoked by deconstrctuor to clean up the __pending_list,
	 * it will delete them directly without cross match to check they 
	 * are hazardous or not, because it's not necessary*/
	void clear_forcely(){
		auto list = __pending_list.exchange(nullptr);
		if(!list)
			return;
		__pending_size = 0;
		std::unordered_set<void*> filter;
		decltype(list) temp;
		decltype(list) new_list = nullptr;
		do{
			temp = list->next;
			auto rst = filter.insert(list->ptr);
			if(rst.second){
				list->deleter(list->ptr);
				delete list;
			}
			list = temp;
		}while(list);
	}


public:
	/* threshold: when the __pending_size reaches it 
	 * will start to delete hazardous pointers. */
	hazard_pointer(size_t threshold = 10000): 
		__threshold_of_clean_up(threshold){};

	~hazard_pointer(){
		clear_forcely();
		auto hn = head();
		if(!hn)
			return;
		decltype(hn) next;
		do{
			next = hn->next;
			delete hn;
		}while((hn = next));
	};

	// size of nodes which this class are buffering.
	inline size_t size(){ return __size;}

	// size of nodes which are pending to deleted.
	inline size_t pending_size(){return __pending_size;}

	inline void set_threshold(size_t threshold){
		__threshold_of_clean_up = threshold;}

	inline size_t threshold(){return __threshold_of_clean_up;}
	
	// get a node for referencing the hazard pointer.
	hazard_node* reference(){
		bool b;
		auto n = head();
		// check if has idle nodes in the link.
		do{
			if(!n)
				break;
			b = false;
			if(n->occupied.compare_exchange_strong(b, true))
				return n;	
			n = n->next;
			continue;
		}while(true);

		// new a node, add it to the link and return it.
		n = new hazard_node();
		n->occupied = true;
		n->next = head();
		while(!__head.compare_exchange_strong(n->next, n));
		++__size;
		return n;
	}
	
	/* add the pointer to __pending_list for later deletion, 
	 * as the __pending_size touches the __threshold_of_clean_up
	 * this pointer may be deleted by delete_ptr_final function. */
	template<typename T>
	void delete_ptr(T* ptr){
		auto n = head();
		if(!n)
			return;
		auto pn = new hazard_pending_node();
		pn->ptr     = ptr;
		pn->next    = __pending_list.load();
		pn->deleter = &hazard_pointer::delete_ptr_final<T>; 
		while(!__pending_list
			.compare_exchange_strong(pn->next,pn));
		++__pending_size;
		check_threshold();
		return ;
	}
};
}

#endif

