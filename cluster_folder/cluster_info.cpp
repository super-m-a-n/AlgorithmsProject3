//file:cluster_info.cpp//
#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <utility>
#include <vector>
#include <array>
#include <random>
#include <set>
#include <limits>
#include <map>
#include <cfloat>
#include "params.hpp"
#include "dataset.hpp"
#include "object.hpp"
#include "cluster_info.hpp"
#include "lsh_struct.hpp"
#include "hypercube_class.hpp"
#include "cbtree.hpp"


Cluster_info::Cluster_info()
{
	// initialize a d-zero vector that will be used to initialize the K centroids
	std::vector <float> input_vector(d);
	for (int i = 0; i < d; ++i)
		input_vector[i] = 0;

	// initialize the vector that will hold the K centroids
	for (int i = 0; i < K; ++i)
	{
		if (algorithm == "Frechet")
		{
			centroids.push_back(new time_series(input_vector));		// centroids are time_series type objects when algorithm is Frechet
		}
		else
		{
			centroids.push_back(new Object(input_vector));			// centroids are Object type objects when algorithm is not Frechet
		}
	}

	// initialize the vector of clusters
	for (int i = 0; i < K; ++i)
	{
		std::vector <const Abstract_Object*> cluster;
		clusters.push_back(cluster);
	}

}

Cluster_info::~Cluster_info()
{
	for (int i = 0; i < K; ++i)
		delete(centroids[i]);		// delete dynamically allocated centroid abstract objects
}


bool Cluster_info::execute(const Dataset & dataset, const std::string & output_file, const std::string & update_method, const std::string & assignment_method, bool complete, bool run_silhouette, double (*metric)(const Abstract_Object &, const Abstract_Object &))
{
	std::ofstream file (output_file, std::ios::out);		// open output file for output operations
	
	if (!file.is_open())			// check if file was opened properly
	    return false;				// error occured

	std::cout << "Initializing Centroids --> ";
	// uses the dataset given, to run K-means++ initialization and find K initial centroids
	K_means_init(dataset, metric);
	std::cout << "Completed\n";

	//start timer for clustering
	auto t_start = std::chrono::high_resolution_clock::now();

	std::cout << "Executing ...\n";

	if (assignment_method == "Classic")
	{
		exact_lloyds(dataset, update_method, metric);
		file << "Algorithm: Assignment --> Lloyds , Update --> " << update_method << " \n";
	}
	else if (assignment_method == "LSH")
	{
		lsh_range_search_clustering(dataset, update_method, metric);
		file << "Algorithm: Assignment --> Range Search LSH , Update --> " << update_method << " \n";
	}
	else if (assignment_method == "Hypercube")
	{
		cube_range_search_clustering(dataset, update_method, metric);
		file << "Algorithm: Assignment --> Range Search Hypercube , Update --> " << update_method << " \n";
	}
	else if (assignment_method == "LSH_Frechet")
	{
		frechet_range_search_clustering(dataset, update_method, metric);
		file << "Algorithm: Assignment --> Range Search Frechet , Update --> " << update_method << " \n";
	}


	// end timer for clustering
	auto t_end = std::chrono::high_resolution_clock::now();
	
	// get execution for clustering in seconds 
	std::chrono::duration <double> time = t_end - t_start;
	
	// write results to file
	for (int i = 0; i < K; ++i)
	{
		file << "CLUSTER-" << i+1 << " {size : " << (this->clusters[i]).size() << " , centroid : ";
		(this->centroids[i])->print_coordinates(file);
		file << " }\n\n";
	}

	// write times of execution in file
	file << "clustering_time : " <<  time.count() << "s\n\n";

	// if silhouette option was given, run silhouette metric as well
	if (run_silhouette == true)
	{
		std::cout << "Calculating Silhouette ...\n";
		
		//start timer for silhouette
		auto s_start = std::chrono::high_resolution_clock::now();
		
		std::vector<double> silhouette = this->silhouette(metric);
		
		// end timer for silhouette
		auto s_end = std::chrono::high_resolution_clock::now();
		
		// get execution for silhouette in seconds 
		std::chrono::duration <double> s_time = s_end - s_start;

		file << "Silhouette: [";
		for (int i = 0 ; i < K ; i++){
			file << silhouette[i] << ", "; 
		}
		file << silhouette[K] << "]\n";
		file << "silhouette_time : " <<  s_time.count() << "s\n\n";
	}

	// if complete option was given, be more verbose
	if (complete == true)
	{
		file << "\n\n\n";
		for (int i = 0; i < K; ++i)
		{
			file << "CLUSTER-" << i+1 << " { ";
			int cl_size = this->clusters[i].size();
			if (cl_size > 0)	// print iff cluster not empty
			{
				for (int j = 0 ; j < (cl_size-1) ; j++){
					file << this->clusters[i][j]->get_name() << ",";
				}
				file << this->clusters[i][cl_size-1]->get_name();
			}
			
			file << "}\n\n";
		}
	}

	return true;
}

void Cluster_info::K_means_init(const Dataset & dataset, double (*metric)(const Abstract_Object &, const Abstract_Object &))
{
	// following block of code picks a uniformly random integer that will serve as the initial centroid index
	const int num_of_Objects = dataset.get_num_of_Objects();

	std::random_device rd;
	std::mt19937 generator(rd());
	std::uniform_int_distribution<int> distr(0, num_of_Objects - 1);	
		
	int initial_centroid = distr(generator);		// pick a random index as the initial centroid index
	int centroid_index = initial_centroid;
	int t = 1;

	// set initial centroid
	centroids[0]->set(dataset.get_ith_object(initial_centroid));

	// array D will hold min distance to some centroid for every Abstract Object
	std::vector <float> D(num_of_Objects);
	// array holding for each object, its nearest centroid thus far (if object is centroid, nearest centroid is itself) 
	std::vector <int> nearest_centroid(num_of_Objects);
	// nearest centroid for the initial centroid is itself
	nearest_centroid[initial_centroid] = initial_centroid;
	D[initial_centroid] = 0;

	// repeat until K centroids have been selected
	while (t < K)
	{
		// array that will hold the partial sums, for each non centroid Abstract Object
		std::vector <float> P(num_of_Objects - t + 1);
		// array that will hold the Abstract Object index of each partial sum in P
		std::vector <int> object_index(num_of_Objects - t + 1);

		int index = 1;
		float max_Di = 0;

		for (int i = 0; i < num_of_Objects; ++i)
		{
			// initialize D array of distances and nearest_centroid array on first iteration
			if (t == 1 && i != initial_centroid)
			{
				D[i] = FLT_MAX;
				nearest_centroid[i] = -1;
			}

			// for each non-centroid Abstract Object			
			if (nearest_centroid[i] != i)
			{
				const Abstract_Object * object = & dataset.get_ith_object(i);
				float dist;

				// if distance to newly added centroid, is smaller than min distance to any centroid thus far
				if ((dist = (*metric)(*centroids[t-1], *object)) < D[i])
				{
					D[i] = dist;								// update min distance
					nearest_centroid[i] = centroid_index;		// update nearest centroid
				}

				max_Di =  (D[i] > max_Di) ? D[i] : max_Di;	// update max Di
				object_index[index++] = i;					// save non centroid object's index
			}
		}

		
		// calculate the array of partial sums P
		P[0] = 0; object_index[0] = 0;
		
		for (int i = 1; i < num_of_Objects - t + 1; i++)	
		{
			P[i] = P[i-1] + (D[object_index[i]] * D[object_index[i]]) / (max_Di * max_Di);	// we divide each D[i] by max_Di to avoid P[i] being very large
		}

		// now we pick a uniformly distributed float x in [0, P[num_of_Objects - t]]
		// picks a uniformly random integer that will serve as the initial centroid
		std::random_device rd_float;
		std::mt19937 generator_float(rd_float());
		std::uniform_real_distribution<float> distr_float(0, P[num_of_Objects - t]);

		float x = distr_float(generator_float);

		// use binary search to find r (object index) in P, that maximizes probabilty proportional to D(i)^2
		// the r found, will be the index of the new centroid to be added
		int r = binary_search(P, x, 0, num_of_Objects - t);

		// update centroid_index to be equal to index of new centroid
		centroid_index = object_index[r];
		t++;
		// set new centroid
		centroids[t-1]->set(dataset.get_ith_object(centroid_index));
		nearest_centroid[centroid_index] = centroid_index;
		D[centroid_index] = 0;
	}	
}

int binary_search(const std::vector <float> & P, float x, int lower_index, int upper_index)
{
	if (upper_index >= lower_index)
	{
		int r = lower_index + (upper_index - lower_index) / 2;
		if (r == 0)
			return 1;

		if ( P[r - 1] < x && x <= P[r] )
			return r;

		if (x > P[r])
			return binary_search(P, x, r+1, upper_index);

		if (x <= P[r - 1])
			return binary_search(P, x, lower_index, r - 1);
	}

	return 0;
}



// clustering using exact lloyd's as assignment method
void Cluster_info::exact_lloyds(const Dataset & dataset, const std::string & update_method, double (*metric)(const Abstract_Object &, const Abstract_Object &))
{
	bool converged = false;

	while (!converged)
	{
		// clear previous clusters
		for (int i = 0; i < K; ++i)
		{
			(this->clusters[i]).clear();
		}

		int num_of_Objects = dataset.get_num_of_Objects();

		// create new clusters by assigning each object to its exact nearest centroid
		for (int i = 0; i < num_of_Objects; ++i)
		{
			const Abstract_Object * object = & dataset.get_ith_object(i);
			double min_dist = (*metric)(*object, *centroids[0]);
			int cluster_index = 0;

			for (int j = 1; j < K; ++j)
			{
				double dist = (*metric)(*object, *centroids[j]);
				if (dist < min_dist)
				{
					min_dist = dist;		// update min distance
					cluster_index = j;		// update cluster index
				}

			}

			// insert object to cluster of exact nearest centroid
			(this->clusters[cluster_index]).push_back(object);
		}

		// update centroids and update converged value
		converged = this->update(update_method, metric);
	}

}

//A general way to compute the min and max
template<typename T>
T min(T x, T y){
	return (x < y) ? x : y;
}

template<typename T>
T max(T x, T y){
	return (x > y) ? x : y;
}

// clustering using lsh range search as assignment method
void Cluster_info::lsh_range_search_clustering(const Dataset & dataset, const std::string & update_method, double (*metric)(const Abstract_Object &, const Abstract_Object &)){
	bool converged = false;

	//No need for something complicated, just calculate the distances and make the necessary update, exact_lloyds does that
	if (K == 1){
		this->exact_lloyds(dataset, update_method, metric);
		return;
	}

	int num_of_Objects = dataset.get_num_of_Objects();

	lsh_struct lsh (floor((double) num_of_Objects/16));

	lsh.import_data(dataset);

	

	while (!converged)
	{
		// initialize a map which will contain all the points (their id is the key) aready visited and their closest centroid and the distance from that centroid
		std::map <std::string, std::pair<double,int>> map;
		std::map<std::string, std::pair<double, int>>::iterator it;

		// clear previous clusters
		for (int i = 0; i < K; ++i){
			(this->clusters[i]).clear();
		}
		//Initialise R
		int R = this->centroids[K-2]->euclidean_distance(*(this->centroids[K-1]));
		for (int i = 0 ; i < K ; i++){
			for (int j = i+1 ; j < K ; j++){
				R = min(R, (int) this->centroids[i]->euclidean_distance(*(this->centroids[j])));
			}
		}
		R /= 2;

		int R2 = 0;

		while(1){

			//Used to check whether any centroid added a point to its cluster
			//If the flag does not become true, it means that no new points are added so add the rest via Lloyd's method
			bool flag = false;

			//For each centroid use range search 
			for (int i = 0 ; i < K ; i++){

				//Execute range search and for each item in the set check if it is already in the map (if so, check which centroid is currently the closest)
				// otherwise it has been found just now so add it to the map and do flag = true
				for (auto item : lsh.range_search(*(this->centroids[i]), R, metric, R2)){
					double dist = std::get<0>(item);
					const Abstract_Object* obj_p = std::get<1>(item);
					std::string id = obj_p->get_name();
					it = map.find(id);

					//If the item has not been inserted yet then add it
					// It also means that the currently closest centroid is the i-th one
					// In the other case, it already is in the map but the distance of the object is closest to the i-th centroid so change the information
					if (it == map.end()){
						flag = true;
						map[id] = std::make_pair(dist, i);
					}
					else if (std::get<-0>(map[id]) > dist){
						it->second.first = dist;
						it->second.second = i;
					}
				}
			}

			if (flag == false) break;

			R2 = R;
			R *= 2;

		}

		//Find the points which are already not assigned and run the exact_lloyds variant for them
		// For every other point, since it is already in the map, we know which cluster is closest to it
		// create new clusters by assigning each object to its exact nearest centroid
		for (int i = 0; i < num_of_Objects; ++i)
		{
			const Abstract_Object * object = & dataset.get_ith_object(i);

			std::string id = object->get_name();

			it = map.find(id);
			
			//If the object is already in the map just see which centroid is closest to it
			if (it != map.end()){
				this->clusters[it->second.second].push_back(object);
				continue;
			}


			double min_dist = (*metric)(*object, *centroids[0]);
			int cluster_index = 0;

			for (int j = 1; j < K; ++j)
			{
				double dist = (*metric)(*object, *centroids[j]);
				if (dist < min_dist)
				{
					min_dist = dist;		// update min distance
					cluster_index = j;		// update cluster index
				}

			}

			// insert object to cluster of exact nearest centroid
			(this->clusters[cluster_index]).push_back(object);
		}


		// update centroids and update converged value
		converged = this->update(update_method, metric);	
	}

}

// clustering using hypercube range search as assignment method
void Cluster_info::cube_range_search_clustering(const Dataset & dataset, const std::string & update_method, double (*metric)(const Abstract_Object &, const Abstract_Object &)){
	bool converged = false;

	//No need for something complicated, just calculate the distances and make the necessary update, exact_lloyds does that
	if (K == 1){
		this->exact_lloyds(dataset, update_method, metric);
		return;
	}

	int num_of_Objects = dataset.get_num_of_Objects();

	hypercube cube;

	cube.import_data(dataset);

	

	while (!converged)
	{
		// initialize a map which will contain all the points (their id is the key) aready visited and their closest centroid and the distance from that centroid
		std::map <std::string, std::pair<double,int>> map;
		std::map<std::string, std::pair<double, int>>::iterator it;

		// clear previous clusters
		for (int i = 0; i < K; ++i){
			(this->clusters[i]).clear();
		}
		//Initialise R
		int R = this->centroids[K-2]->euclidean_distance(*(this->centroids[K-1]));
		for (int i = 0 ; i < K ; i++){
			for (int j = i+1 ; j < K ; j++){
				R = min(R, (int) this->centroids[i]->euclidean_distance(*(this->centroids[j])));
			}
		}
		R /= 2;

		int R2 = 0;

		while(1){

			//Used to check whether any centroid added a point to its cluster
			//If the flag does not become true, it means that no new points are added so add the rest via Lloyd's method
			bool flag = false;

			//For each centroid use range search 
			for (int i = 0 ; i < K ; i++){

				//Execute range search and for each item in the set check if it is already in the map (if so, check which centroid is currently the closest)
				// otherwise it has been found just now so add it to the map and do flag = true
				for (auto item : cube.range_search(*(this->centroids[i]), R, metric, R2)){
					double dist = std::get<0>(item);
					const Abstract_Object* obj_p = std::get<1>(item);
					std::string id = obj_p->get_name();
					it = map.find(id);

					//If the item has not been inserted yet then add it
					// It also means that the currently closest centroid is the i-th one
					// In the other case, it already is in the map but the distance of the object is closest to the i-th centroid so change the information
					if (it == map.end()){
						flag = true;
						map[id] = std::make_pair(dist, i);
					}
					else if (std::get<-0>(map[id]) > dist){
						it->second.first = dist;
						it->second.second = i;
					}
				}
			}

			if (flag == false) break;

			R2 = R;
			R *= 2;

		}

		//Find the points which are already not assigned and run the exact_lloyds variant for them
		// For every other point, since it is already in the map, we know which cluster is closest to it
		// create new clusters by assigning each object to its exact nearest centroid
		for (int i = 0; i < num_of_Objects; ++i)
		{
			const Abstract_Object * object = & dataset.get_ith_object(i);

			std::string id = object->get_name();

			it = map.find(id);
			
			//If the object is already in the map just see which centroid is closest to it
			if (it != map.end()){
				this->clusters[it->second.second].push_back(object);
				continue;
			}


			double min_dist = (*metric)(*object, *centroids[0]);
			int cluster_index = 0;

			for (int j = 1; j < K; ++j)
			{
				double dist = (*metric)(*object, *centroids[j]);
				if (dist < min_dist)
				{
					min_dist = dist;		// update min distance
					cluster_index = j;		// update cluster index
				}

			}

			// insert object to cluster of exact nearest centroid
			(this->clusters[cluster_index]).push_back(object);
		}


		// update centroids and update converged value
		converged = this->update(update_method, metric);	
	}
}

// clustering using frechet range search as assignment method
void Cluster_info::frechet_range_search_clustering(const Dataset & dataset, const std::string & update_method, double (*metric)(const Abstract_Object &, const Abstract_Object &))
{
	bool converged = false;

	//No need for something complicated, just calculate the distances and make the necessary update, exact_lloyds does that
	if (K == 1){
		this->exact_lloyds(dataset, update_method, metric);
		return;
	}

	int num_of_Objects = dataset.get_num_of_Objects();

	lsh_struct lsh_frechet (floor((double) num_of_Objects/16));

	lsh_frechet.import_data(dataset);

	

	while (!converged)
	{
		// initialize a map which will contain all the points (their id is the key) aready visited and their closest centroid and the distance from that centroid
		std::map <std::string, std::pair<double,int>> map;
		std::map<std::string, std::pair<double, int>>::iterator it;

		// clear previous clusters
		for (int i = 0; i < K; ++i){
			(this->clusters[i]).clear();
		}
		//Initialise R
		int R = metric(*this->centroids[K-2],*(this->centroids[K-1]));
		for (int i = 0 ; i < K ; i++){
			for (int j = i+1 ; j < K ; j++){
				R = min(R, (int) metric(*this->centroids[i],*(this->centroids[j])));
			}
		}
		R /= 2;

		std::set<std::string> visited_set;

		while(1){

			//Used to check whether any centroid added a point to its cluster
			//If the flag does not become true, it means that no new points are added so add the rest via Lloyd's method
			bool flag = false;

			//For each centroid use range search 
			for (int i = 0 ; i < K ; i++){

				//Execute range search and for each item in the set check if it is already in the map (if so, check which centroid is currently the closest)
				// otherwise it has been found just now so add it to the map and do flag = true
				for (auto item : lsh_frechet.range_search_with_set(*(this->centroids[i]), R, visited_set, metric)){
					double dist = std::get<0>(item);
					const Abstract_Object* obj_p = std::get<1>(item);
					std::string id = obj_p->get_name();
					it = map.find(id);

					//If the item has not been inserted yet then add it
					// It also means that the currently closest centroid is the i-th one
					// In the other case, it already is in the map but the distance of the object is closest to the i-th centroid so change the information
					if (it == map.end()){
						flag = true;
						map[id] = std::make_pair(dist, i);
					}
					else if (std::get<-0>(map[id]) > dist){
						it->second.first = dist;
						it->second.second = i;
					}
				}
			}

			if (flag == false) break;

			R *= 2;

		}

		//Find the points which are already not assigned and run the exact_lloyds variant for them
		// For every other point, since it is already in the map, we know which cluster is closest to it
		// create new clusters by assigning each object to its exact nearest centroid
		for (int i = 0; i < num_of_Objects; ++i)
		{
			const Abstract_Object * object = & dataset.get_ith_object(i);

			std::string id = object->get_name();

			it = map.find(id);
			
			//If the object is already in the map just see which centroid is closest to it
			if (it != map.end()){
				this->clusters[it->second.second].push_back(object);
				continue;
			}


			double min_dist = (*metric)(*object, *centroids[0]);
			int cluster_index = 0;

			for (int j = 1; j < K; ++j)
			{
				double dist = (*metric)(*object, *centroids[j]);
				if (dist < min_dist)
				{
					min_dist = dist;		// update min distance
					cluster_index = j;		// update cluster index
				}

			}

			// insert object to cluster of exact nearest centroid
			(this->clusters[cluster_index]).push_back(object);
		}


		// update centroids and update converged value
		converged = this->update(update_method, metric);	
	}
}


bool Cluster_info::update(const std::string & update_method, double (*metric)(const Abstract_Object &, const Abstract_Object &))
{
	double e_euclid = 1;			// testing required
	double e_frechet = 20;			// testing required
	double avg_deviation = 0.0;
	int max_iters = 12;
	static int iters = 0;

	// for each cluster
	for (int i = 0; i < K; ++i)
	{
		// compute mean of cluster
		const Abstract_Object * cluster_mean = mean(update_method, this->clusters[i]);

		// if cluster is empty, mean is null, just continue, avg_deviation, centroid remain unchanged
		if (cluster_mean == nullptr)
			continue;
		
		// calculate average change across all centroids
		avg_deviation += (*metric)(*cluster_mean, *(this->centroids[i])) / K;
		
		// set new centroid of cluster to be the cluster_mean
		(this->centroids[i])->set(*cluster_mean);

		// delete mean , no longer needed
		delete(cluster_mean);
	}

	iters++;
	//std::cout << "Iteration - average deviation --> " << avg_deviation << std::endl;

	if (iters >= max_iters)		// iteration threshold, in case of bouncing around local minima, deviation not becoming small enough
		return true;

	if (update_method == "Mean Frechet" && avg_deviation < e_frechet)		// if average change across all centroids is adequately small
		return true;		// algorithm has converged
	else if (update_method == "Mean Vector" && avg_deviation < e_euclid)	// if average change across all centroids is adequately small
		return true;		// algorithm has converged
	else
		return false;		// otherwise keep iterating
	
}

Abstract_Object * mean(const std::string & update_method, const std::vector <const Abstract_Object*> & cluster)
{
	// check for empty cluster
	if (cluster.size() == 0)
		return nullptr;
	
	// continue, cluster not empty ==> mean curve/vector are defined

	if (update_method == "Mean Frechet")	// Mean Curve is used
	{
		// create a complete binary tree with curves of cluster as leafs
		CBTree CBT(cluster);
		// traverse complete binary tree in post order fashion, to find the mean curve in root, and return it
		return CBT.post_order_traversal();
	}
	else	// Mean Vector is used
	{
		std::vector <float> mean_vector(d, 0.0);
		int T = cluster.size();		// get number of objects in cluster

		// for each object in cluster
		for (auto const& abstract_object : cluster)
		{
			const Object* object  = dynamic_cast<const Object *>(abstract_object);
			for (int j = 0; j < d; j++)
			{
				// update mean vector
				mean_vector[j] += object->get_ith(j) / T;
			}
			
		}

		// create and return the mean vector Object
		return new Object(mean_vector);
	}
}


// finds silhouette for each cluster, and for clustering in total
std::vector <double> Cluster_info::silhouette(double (*metric)(const Abstract_Object &, const Abstract_Object &)) const{
	
	double s_total = 0;

	int total_size = 0;

	std::vector<double> silhouette;
	
	//For each cluster
	for (int i = 0 ; i < K ; i++){
		//The cluster_evaluation
		double cl_eval = 0.0;

		int cl_size = this->clusters[i].size();

		total_size += cl_size;

		for (int j = 0 ; j < cl_size ; j++){

			const Abstract_Object* obj_p = this->clusters[i][j];
			
			double a_i = 0.0, b_i = 0.0;
			
			for (int k = 0 ; k < cl_size ; k++){
				if (j == k) continue;

				a_i += (*metric)(*obj_p, *(this->clusters[i][k]));
			}
			if (cl_size != 1)
				a_i /= cl_size -1;

			double min_dist = DBL_MAX;
			int cluster_index = 0;

			for (int k = 0; k < K; ++k)
			{
				if (k == i) continue;
				double dist = (*metric)(*obj_p, *centroids[k]);
				if (dist < min_dist)
				{
					min_dist = dist;		// update min distance
					cluster_index = k;		// update cluster index
				}

			}

			int b_cl_size = this->clusters[cluster_index].size();

			for (int k = 0 ; k < b_cl_size ; k++){

				b_i += (*metric)(*obj_p, *(this->clusters[cluster_index][k]));
			}
			if (b_cl_size != 0)
				b_i /= b_cl_size;

			double t = max(a_i,b_i);

			double s_i = 0.0;

			if (t != 0)
				s_i = (b_i -a_i)/t;
			

			cl_eval += s_i ;

			s_total += s_i ;
		}

		cl_eval /= cl_size -1;

		silhouette.push_back(cl_eval);

	}
	silhouette.push_back(s_total/ total_size);
	return silhouette;
}		