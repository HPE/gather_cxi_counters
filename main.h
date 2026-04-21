#ifndef MAIN_H
#define MAIN_H

#include <iostream>
#include "zmq.hpp"
#include <unistd.h>
#include <cstring>
#include <vector>
#include <string>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/wait.h>
#include <map>
#include <iomanip>
#include <cstdio>
#include <dirent.h>
#include <chrono>
#include <numeric>
#include <tuple>

#define ZMQ_PORT "5555"

#include "utils.h"
#include "counter_collection.h"
#include "counter_aggregation.h"

// Helper to get all nodes from scontrol
std::vector<std::string> get_node_list_from_scontrol();

#endif
