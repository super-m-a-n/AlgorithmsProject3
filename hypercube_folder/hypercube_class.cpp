#include "hypercube_class.hpp"
#include <string>
#include <chrono>
#include <queue>
#include <utility>
#include <vector>
#include <fstream>

hypercube::hypercube(){
    this->cube_array = new std::list<const Abstract_Object*>[1 << d1]; //size 2^k (di is the global variable which stores the dimension of the hypercube)
    this->f_array = new f_hash[d1];
    this->h_array = new h_hash[d1];
}

uint8_t hypercube::get_0_or_1(int index, const Abstract_Object& abstract_object){
	// downcast abstract object to type Object (hypercube works with only Objects anyway)
	const Object& object = dynamic_cast<const Object&>(abstract_object);
    return (this->f_array[index])(this->h_array[index](object));
}

//For each object, index is which vertex of the hypercube the object will be stored, encoded in the rightmost k digits
// e.g if index = 0b001011 and k = 5 that means the object will be inserted at the vertex (0,1,0,1,1) where 
// the 5 rightmost bits are read from left to right
void hypercube::import_data(const Dataset& dataset){
    int num_of_objects = dataset.get_num_of_Objects();
    for (int i = 0 ; i < num_of_objects ; i++ ){
        int index = 0; //the index holds the encoding of the coordinates of the vertex 
        const Abstract_Object& obj = dataset.get_ith_object(i); //Get the i-th object of the dataset
        for (int j = 0 ; j < d1 ; j++){
            index = (index << 1) + this->get_0_or_1(j, obj);
        }
        this->cube_array[index].push_back(&obj);  //Add the object to the vertex (given by the index) 
    }
}
//void print() const;

bool hypercube::execute(const Dataset & dataset, const Dataset & query_dataset, const std::string & output_file, const int & N, const int & R, double (*metric)(const Abstract_Object &, const Abstract_Object &)){

	std::ofstream file (output_file, std::ios::out);		// open output file for output operations
	
	if (!file.is_open())			// check if file was opened properly
	    return false;				// error occured

	int num_of_Objects = query_dataset.get_num_of_Objects();
	
	// counters for metrics
	double sum_dist_cube = 0;
	double sum_dist_true = 0;
	double avg_AF = 0;
	double avg_TF = 0;
	double tApprAvg = 0;
	double tTrueAvg = 0;
	double max_AF = 0;
	int not_found = 0;

	for (int i = 0; i < num_of_Objects; i++)		// run for each of the query Objects
	{
		file << "Query: query Object " << (query_dataset.get_ith_object(i)).get_name() << "\n";
		file << "Algorithm: Hypercube  \n\n";

		//start timer for lsh
		auto t_cube_start = std::chrono::high_resolution_clock::now();
		
		// run approximate nearest neighbors and return the neighbors and the distances found
		std::vector <std::pair <double, const Abstract_Object*> > appr_nearest = this->appr_nearest_neighbors(dataset, query_dataset.get_ith_object(i), N, metric);
		// end timer for lsh
		auto t_cube_end = std::chrono::high_resolution_clock::now();

		// start timer for brute force
		auto t_true_start = std::chrono::high_resolution_clock::now();

		// run exact nearest neighbors and return the neighbors and the distances found
		std::vector <std::pair <double, const Abstract_Object*> > exact_nearest = this->exact_nearest_neighbors(dataset, query_dataset.get_ith_object(i), N, metric);
		
		// end timer for brute force
		auto t_true_end = std::chrono::high_resolution_clock::now();

		for (int index = 0; index < N; ++index)	
		{
			double dist_cube = 0, dist_true = 0, AF = 0;
			bool found = false;

			if ((int) appr_nearest.size() >= index + 1)	// neighbors found may be less than N
			{
				const Abstract_Object * object = std::get<1>(appr_nearest[index]);			// get object-neighbor found
				double dist = std::get<0>(appr_nearest[index]);								// get distance from query object
				sum_dist_cube += dist;
				dist_cube = dist;
				found = true;

				file << "Approxmimate Nearest neighbor-" << index + 1 << " : Object " << object->get_name() << '\n';	//write to file
				file << "distanceApproximate : " << dist << '\n';
			}
			else
				not_found++;
			//else{
			//	std::cout << "Appr_nearest.size() = " << appr_nearest.size() <<  " is smaller than N = " << N << std::endl;
			//s}

			if ((int) exact_nearest.size() >= index + 1)	// neighbors found may be less than N
			{
				const Abstract_Object * object = std::get<1>(exact_nearest[index]);			// get object-neighbor found
				double dist = std::get<0>(exact_nearest[index]);							// get distance from query object
				sum_dist_true += dist;
				dist_true = dist;

				file << "True Nearest neighbor-" << index + 1 << " : Object " << object->get_name() << '\n';
				file << "distanceTrue : " << dist << "\n\n";			// write to file
			}

			if (found)
			{
				AF = dist_cube/dist_true;
				if (AF > max_AF)
					max_AF = AF;
				avg_AF += AF;
			}
		}

		// get execution for LSH in milliseconds 
	    std::chrono::duration <double, std::milli> tCube = t_cube_end - t_cube_start;
	    // get execution for brute force in milliseconds
	    std::chrono::duration <double, std::milli> tTrue = t_true_end - t_true_start;
	    // write times of execution in file
	    file << "tHypercube : " << tCube.count() << "ms\n";
	    file << "tTrue : " << tTrue.count() << "ms\n\n";
		
		avg_TF += tCube.count() / tTrue.count();
		tApprAvg += tCube.count();
		tTrueAvg += tTrue.count();

		file << "tHypercube / tTrue: " << (double) tCube.count() / (double) tTrue.count() << std::endl;

		if (R != 0)	// if given Range for range search is 0 skip range search
		{
			file << "R-near neighbors: (R = " << R << ")" << '\n';

			// run approximate range search and write results into file
			std::list <std::pair <double, const Abstract_Object*> > R_list = this->range_search(query_dataset.get_ith_object(i), R, metric);
			
			for (auto item: R_list){
				// object is within range, so ass it to the list
					file << "Object " << (std::get<1>(item))->get_name() << '\n';
					;
			}
			R_list.clear();
		}
		file << "\n\n";
	}

	// print metrics to file
	file << "tApproximateAverage: " << tApprAvg/num_of_Objects << '\n';
	file << "tTrueAverage: " << tTrueAvg/num_of_Objects << '\n';
	file << "MAF: " << max_AF << "\n\n";

	//print metrics to std::out
	std::cout << "\n\nSum dist true / Sum dist cube = " << sum_dist_true / sum_dist_cube << std::endl;
	std::cout << "Max AF = " << max_AF << std::endl;
	std::cout << "Average AF = " << avg_AF / (N * num_of_Objects - not_found) << std::endl;
	std::cout << "Average Time Fraction (Cube/True) = " << avg_TF/num_of_Objects << std::endl;
	std::cout << "Not found = " << not_found << std::endl << std::endl;
	
	return true;
}


//The function recursively iterates through all vertices with increasing hamming distance until all are checked or M_rem or probes_rem becomes 0

void hypercube::vertex_visiting_first_stage(search_type Type, const int R, int curr_vertex, int ham_dist, int M_rem, int probes_rem, uint curr_bit,
	void* max_heap,
								const Abstract_Object & query_object, double (*metric)(const Abstract_Object &, const Abstract_Object &), int N, const int R2){

	this->vertex_visiting_second_stage(Type, R, curr_vertex, M_rem, probes_rem, curr_bit, ham_dist, max_heap, query_object, metric, N, R2);

	//If all the allowed nodes or verices have been checked or
	//if we checked the vertex with all the bits having changed, which implies all the vertices have been visited, then end the recursion
	if (M_rem == 0 || probes_rem == 0 || ham_dist == d1) return;

	this->vertex_visiting_first_stage(Type, R, curr_vertex, ham_dist +1, M_rem, probes_rem, curr_bit, max_heap, query_object, metric, N, R2);


}
void hypercube::vertex_visiting_second_stage(search_type Type, const int R, int curr_vertex, int& M_rem, int& probes_rem, uint curr_bit, int ham_rem,
	void* max_heap,
								const Abstract_Object & query_object, double (*metric)(const Abstract_Object &, const Abstract_Object &), int& N, const int R2 ){


	if (ham_rem == 0){
		this->vertex_visiting_third_stage(Type, R, curr_vertex, M_rem, max_heap, query_object, metric, N, R2);
		probes_rem -= 1;
		return;
	}

	//If there at at least ham_rem  bits left besides the curr_bit, it is possible to not change the curr_bit of the curr_vertex and change others
	if ((curr_bit >> ham_rem) != 0){
		this->vertex_visiting_second_stage(Type, R, curr_vertex, M_rem, probes_rem, curr_bit >> 1, ham_rem, max_heap, query_object, metric, N, R2);
		if (M_rem == 0 || probes_rem == 0) return;
	}

	this->vertex_visiting_second_stage(Type, R, curr_vertex xor curr_bit, M_rem, probes_rem, curr_bit >> 1, ham_rem -1, max_heap, query_object, metric, N, R2);
}

void hypercube::vertex_visiting_third_stage(search_type Type, const int R, int curr_vertex, int& M_rem, void* max_heap,
								const Abstract_Object & query_object, double (*metric)(const Abstract_Object &, const Abstract_Object &), int& N, const int R2){

	std::list<const Abstract_Object*>& vertex_list = this->cube_array[curr_vertex];
	for(auto obj_p : vertex_list){
		double dist = (*metric)(query_object, *obj_p);

		// If the search type is kNN we don't care about the distance, just put it in the heap.
		// Otherwise, the search type is range search thus we want the distance to be smaller than R
		if (Type == kNN){
			
			push_at_most_N(obj_p, N, dist, (std::priority_queue <std::pair <double, const Abstract_Object*> >* ) max_heap);
		}
		// If the point belongs to the ring [R2, R) then add it to the list
		else if (R2 <= dist && dist < R){

				std::list <std::pair <double, const Abstract_Object*> >* casted_p = (std::list <std::pair <double, const Abstract_Object*> >* ) max_heap;
				casted_p->push_back(std::make_pair(dist, obj_p));
		}
		M_rem -= 1;
		if (M_rem == 0) return;
	}
}

void push_at_most_N(const Abstract_Object* obj_p, int N, double dist, std::priority_queue <std::pair <double, const Abstract_Object*> >* max_heap){
	if ((int) max_heap->size() < N)	// if we haven't found N neighbors yet
		max_heap->push(std::make_pair(dist, obj_p));	// simply push the new object-neighbor found
	else{
		// N neighbors have already been found, so compare with largest distance
		double largest_dist = std::get<0>(max_heap->top());
		if (dist < largest_dist){	// if new object is closer
			max_heap->pop();									// pop the Object with the largest distance
			max_heap->push(std::make_pair(dist, obj_p));	// and insert the new object
		}
	}
}

std::vector <std::pair <double, const Abstract_Object*> > hypercube::appr_nearest_neighbors(const Dataset & dataset, const Abstract_Object & query_object, const int & N, double (*metric)(const Abstract_Object &, const Abstract_Object &))
{

	// run approximate kNN

	// initialize a min heap priority queue, that will store the Object identifier string name and the distance of Object from query object
	std::priority_queue <std::pair <double, const Abstract_Object*> > max_heap;

	int query_vertex = 0;

	for (int j = 0 ; j < d1 ; j++){
            query_vertex = (query_vertex << 1) + this->get_0_or_1(j, query_object);
    }

	this->vertex_visiting_first_stage(kNN, 0, query_vertex, 0, M, probes, 1 << (d1-1), (void*) & max_heap, query_object, metric, N);

	// initialize a vector with how many exact nearest neighbors were found
	std::vector <std::pair <double, const Abstract_Object*> > nearest(max_heap.size());

	for (int i = nearest.size() - 1; i >= 0; --i)	// for each nearest neighbor found
	{
		nearest[i] = max_heap.top();	// save nearest neighbor
		max_heap.pop();
	}

	return nearest;
}


std::list <std::pair <double, const Abstract_Object*> > hypercube::range_search(const Abstract_Object & query_object, const int & R, double (*metric)(const Abstract_Object &, const Abstract_Object &),  const int R2)
{

	int query_index = 0;

	for (int j = 0 ; j < d1 ; j++){
            query_index = (query_index << 1) + this->get_0_or_1(j, query_object);
    }

	std::list <std::pair <double, const Abstract_Object*> > R_list;

	this->vertex_visiting_first_stage(RANGE_SEARCH, R, query_index, 0, M, probes, 1 << (d1-1), (void*) & R_list, query_object, metric,0, R2);

	return R_list;
}

std::vector <std::pair <double, const Abstract_Object*> > hypercube::exact_nearest_neighbors(const Dataset & dataset, const Abstract_Object & query_object, const int & N, double (*metric)(const Abstract_Object &, const Abstract_Object &)){
	// run brute force exact kNN
	int num_of_Objects = dataset.get_num_of_Objects();

	// initialize a max heap priority queue, that will store the distance of Object from query object and a pointer to the Object itself
	std::priority_queue <std::pair <double, const Abstract_Object*> > max_heap;

	// check each of the dataset objects by brute force
	for (int i = 0; i < num_of_Objects; ++i)
	{
		// find its distance from query object
		double dist = (*metric)(query_object, dataset.get_ith_object(i));

		if ((int) max_heap.size() < N)	// if we haven't found N neighbors yet
			max_heap.push(std::make_pair(dist, & dataset.get_ith_object(i)));
		else
		{
			// N neighbors have already been found, so compare with largest distance
			double largest_dist = std::get<0>(max_heap.top());

			if (dist < largest_dist)	// if new object is closer
			{
				max_heap.pop();									// pop the Object with the largest distance
				max_heap.push(std::make_pair(dist, & dataset.get_ith_object(i)));	// and insert the new object
			}
		}
	}

	// initialize a vector with how many exact nearest neighbors were found
	std::vector <std::pair <double, const Abstract_Object*> > nearest(max_heap.size());
	for (int i = nearest.size() - 1; i >= 0; --i)	// for each nearest neighbor found
	{
		nearest[i] = max_heap.top();	// save nearest neighbor
		max_heap.pop();
	}

	return nearest;
}



hypercube::~hypercube(){
    delete[] this->cube_array;
    delete[] this->f_array;
    delete[] this->h_array;

}
