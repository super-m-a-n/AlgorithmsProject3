//file:dataset.hpp//
#ifndef _DATASET_HPP_
#define _DATASET_HPP_
#include <iostream>
#include <string>
#include "object.hpp"

// class Dataset is simply a collection of Abstract-Objects
class Dataset
{
private:
	Abstract_Object ** dataset; 		// an array of pointers to Abstract-Objects
	int num_of_Objects;

public:
	// constructor uses the input file to initialize the dataset
	Dataset(int num_of_Points, std::string & input_file);
	~Dataset();
	// print method for debugging
	void print() const;
	// returns number of objects of dataset
	int get_num_of_Objects() const;
	// returns i-th object of dataset;
	const Abstract_Object& get_ith_object(int i) const;
	
};

#endif