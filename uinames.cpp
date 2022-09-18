#include "uinames.h"
#include <linux/uinput.h>

std::unordered_map<int, std::string> ev_names = {
#include "ev_codes.h"
};
std::unordered_map<int, std::string> syn_names = {
#include "syn_codes.h"
};
std::unordered_map<int, std::string> key_names = {
#include "key_codes.h"
#include "btn_codes.h"
};
std::unordered_map<int, std::string> key_only_names = {
#include "key_codes.h"
};
std::unordered_map<int, std::string> btn_only_names = {
#include "btn_codes.h"
};
std::unordered_map<int, std::string> rel_names = {
#include "rel_codes.h"
};
std::unordered_map<int, std::string> abs_names = {
#include "abs_codes.h"
};
std::unordered_map<int, std::string> msc_names = {
#include "msc_codes.h"
};

const char* get_name(const std::unordered_map<int, std::string> &m, int v)
{
  auto it = m.find(v);
  return it!=m.end() ? it->second.c_str() : "??";
}

const char* type_name(int t) { return get_name(ev_names, t); }
const char* code_name(int t, int c)
{
  switch (t) {
  case EV_SYN: return get_name(syn_names, c);
  case EV_KEY: return get_name(key_names, c);
  case EV_REL: return get_name(rel_names, c);
  case EV_ABS: return get_name(abs_names, c);
  case EV_MSC: return get_name(msc_names, c);
  default: return "??";
  }
}

bool is_code_key(int c)
{
  return key_only_names.find(c)!=key_only_names.end();
}

bool is_code_mouse(int c)
{
  return c==BTN_LEFT || c==BTN_MIDDLE || c==BTN_RIGHT;
}
