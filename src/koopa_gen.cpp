#include <cassert>
#include <cstdio>
#include <memory>
#include <string>
#include <map>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <stack>
#include "myast.h"

using namespace std;

int koopa_reg = 0;
unordered_map<BaseAST*, int> bbs_begin;
unordered_map<int, int> bbs_end; // 负的说明不用jump
int bbs_num = 1; // 累加量：为了不让基本块名字重复
int bbs_now = 0; // 当前所在基本块
stack<int> while_entry; // 当前基本块的while入口
stack<int> while_end; // 当前基本块的while出口

bool bbs_begin_exit(BaseAST* ptr){
  if(ptr == NULL)
    return false;
  return bbs_begin.find(ptr) != bbs_begin.end();
}

void bbs_begin_insert(BaseAST* ptr, int num){
  if(ptr == NULL)
    return;
  bbs_begin.insert(make_pair(ptr, num));
}

void print_bbs_begin(int num){
  koopa_str = koopa_str + "\n%_" + to_string(num) + ":\n";
}

void Bind(int begin, int end){
  bbs_end.insert(make_pair(begin, end));
}

void Inherit(int predecessor, int successor){
  auto peace = bbs_end.find(predecessor);
  if(peace == bbs_end.end()){
    cout << koopa_str << endl;
    cout << "Impossible!!!" << endl;
  }
  int asset = peace->second;
  bbs_end[predecessor] = -1;

  // 不是新开辟的next就不用继承 新开辟的就得继承
  if(successor == asset)
    return;
  bbs_end.insert(make_pair(successor, asset));
}

void Branch(string cond, int then_bbs, int else_bbs, int end){
  koopa_str = koopa_str + "br " + cond + ", %_" + to_string(then_bbs) + ", %_" + to_string(else_bbs) + "\n";
  print_bbs_begin(then_bbs);
}

void Branch_(string cond, int then_bbs, int end){
  koopa_str = koopa_str + "br " + cond + ", %_" + to_string(then_bbs) + ", %_" + to_string(end) + "\n";
  print_bbs_begin(then_bbs);
}

void Jump(int bbs){
  // 根据bbs_end决定要不要输出jump
  auto peace = bbs_end.find(bbs);
  if(peace == bbs_end.end())
    cout << "what a fuck!!!" << endl;
  if(peace->second < 0)
    return;
  koopa_str = koopa_str + "jump %_" + to_string(peace->second) + "\n";
  peace->second = -1;
}

string Load(string ident){
  koopa_str = koopa_str + "%" + to_string(koopa_reg++) + " = load @" + ident + "\n";
  return "%" + to_string(koopa_reg-1);
}

void Store(string ident, string from){
  koopa_str = koopa_str + "store " + from + ", @" + ident + "\n";
}

void Alloc(string ident){
  koopa_str = koopa_str + "@" + ident + " = alloc i32\n";
}

void Global(string ident, int initval){
  koopa_str = koopa_str + "global @" + ident + "0 = alloc i32, ";
  if(initval == 0)
    koopa_str += "zeroinit\n";
  else
    koopa_str += to_string(initval);
}

V init_tmp_global;
int init_where_global = 0;

void global_assist(V boundary){
  if(boundary.size() == 1){
    koopa_str = koopa_str + to_string(init_tmp_global[init_where_global++]);
    for(int i = 1; i < boundary[0]; i++)
        koopa_str = koopa_str + ", " + to_string(init_tmp_global[init_where_global++]);
    return;
  }

  V tmp;
  tmp.assign(boundary.begin() + 1, boundary.end());
  koopa_str += "{";
  global_assist(tmp);
  koopa_str += "}";
  for(int i = 1; i < boundary[0]; i++){
    koopa_str += ", {";
    global_assist(tmp);
    koopa_str += "}";
  }
}

void GlobalArray(string ident, V init, V boundary){
  int dim_num = boundary.size();
  string str = "global @" + ident + "0 = alloc ";
  for(int i = 0; i < dim_num; i++)
    str += "[";
  str += "i32";
  for(int i = dim_num-1; i >= 0; i--)
    str = str + ", " + to_string(boundary[i]) + "]";
  koopa_str = koopa_str + str + ", ";

  int tmp = init.size();
  if(tmp == 0)
    koopa_str += "zeroinit\n";
  else{
    // 清零
    init_tmp_global.clear();
    init_tmp_global.assign(init.begin(), init.end());
    init_where_global = 0;

    koopa_str += "{";
    global_assist(boundary);
    koopa_str += "}\n";
  }
}

V init_tmp;
int init_where = 0;
bool is_zero = false;

// 初始化的时候用
void Getelemptr(string ident, V boundary){
  if(boundary.size() == 1){
    for(int i = 0; i < boundary[0]; i++){
      koopa_str = koopa_str + "%" + to_string(koopa_reg++) + " = getelemptr " + ident + ", " + to_string(i) + "\n";
      if(is_zero)
        koopa_str = koopa_str + "store 0, %" + to_string(koopa_reg-1) + "\n";
      else
        koopa_str = koopa_str + "store " + to_string(init_tmp[init_where++]) + ", %" + to_string(koopa_reg-1) + "\n";
    }
    return;
  }

  V tmp;
  tmp.assign(boundary.begin() + 1, boundary.end());
  for(int i = 0; i < boundary[0]; i++){
    int tmp_reg = koopa_reg;
    koopa_str = koopa_str + "%" + to_string(koopa_reg++) + " = getelemptr " + ident + ", " + to_string(i) + "\n";
    Getelemptr("%" + to_string(tmp_reg), tmp);
  }
} 

string Getelemptr_only(string ident, vector<string> offset){
  if(offset.size() != 0){
    koopa_str = koopa_str + "%" + to_string(koopa_reg++) + " = getelemptr " + ident + ", " + offset[0] + "\n";
    for(int i = 1; i < offset.size(); i++){
      koopa_str = koopa_str + "%" + to_string(koopa_reg) + " = getelemptr %" + to_string(koopa_reg-1) + ", " + offset[i] + "\n";
      koopa_reg++;
    }
    return "%" + to_string(koopa_reg-1);
  }
  return ident;
}

void AllocArray(string ident, V init, V boundary){
  int dim_num = boundary.size();
  string str = "@" + ident + " = alloc ";
  for(int i = 0; i < dim_num; i++)
    str += "[";
  str += "i32";
  for(int i = dim_num-1; i >= 0; i--)
    str = str + ", " + to_string(boundary[i]) + "]";
  koopa_str = koopa_str + str + "\n";

  int tmp = init.size();
  if(tmp == 0){
    // 清零
    is_zero = true;
  }
  else{
    // 清零
    is_zero = false;
    init_tmp.clear();
    init_tmp.assign(init.begin(), init.end());
    init_where = 0;
  }
  Getelemptr("@" + ident, boundary);
}

map<bool, string> P = {{true, " %"}, {false, " "}};
map<string, string> K = {{"!", "eq"}, {"+", "add"}, {"-", "sub"}, {"*", "mul"}, {"/", "div"}, {"%", "mod"},
{"|", "or"}, {"&", "and"}, {"!=", "ne"}, {"==", "eq"}, 
{">", "gt"}, {"<", "lt"}, {">=", "ge"}, {"<=", "le"}};

koo_ret koo_parse(string str){
  koo_ret ret;
  ret.val = -1;
  if(str[0] == '%'){
    ret.reg = true;
    str.erase(str.begin());
  }
  else
    ret.reg = false;
  ret.val = atoi(str.c_str());
  return ret;
}

string koo_binary(koo_ret l, koo_ret r, string op){
    koopa_str += "%";
    koopa_str = koopa_str + to_string(koopa_reg++) + " = " + K[op] + 
    P[l.reg] + to_string(l.val) + "," + P[r.reg] + to_string(r.val) + "\n";
    return "%" + to_string(koopa_reg-1);
}
