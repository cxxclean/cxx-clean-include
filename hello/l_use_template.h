#ifndef _use_template_h_
#define _use_template_h_

#include "l.h"

#define _SILENCE_STDEXT_HASH_DEPRECATION_WARNINGS

#include <vector>
#include <map>
#include <set>
#include <hash_map>

using std::map;
using std::set;

typedef std::vector<L> L_Vec;
typedef map<int, L1> L1_Map;
typedef map<int, L2> L2_Map;
typedef set<L3> L3_Set;
typedef map<int, std::vector<std::map<int, std::map<int, std::set<L4>>>>> L4_Map;
typedef stdext::hash_map<int, L5> L5_HashMap;

#endif // _use_template_h_