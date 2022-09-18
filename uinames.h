#ifndef UINAMES_H_
#define UINAMES_H_

#include <string>
#include <unordered_map>

extern std::unordered_map<int, std::string> ev_names;
extern std::unordered_map<int, std::string> syn_names;
extern std::unordered_map<int, std::string> key_names;
extern std::unordered_map<int, std::string> rel_names;
extern std::unordered_map<int, std::string> abs_names;
extern std::unordered_map<int, std::string> msc_names;

const char* type_name(int t);
const char* code_name(int t, int c);

bool is_code_key(int c);
bool is_code_mouse(int c);

#endif /* UINAMES_H_ */
