#include <cassert>
#include <cstdio>
#include <memory>
#include <string>
#include <map>
#include <vector>
#include "myast.h"

using namespace std;

Area* top; // 各作用域符号表
vector<string> para; // 函数形参
unordered_map<string, V> para_array; // 函数形参：数组 
int tag = 1;
bool whether_load = true; // 实在是后知后觉数组不一定要提取出值，还有可能是函数调用的传参
unordered_map<string, bool> ret_func;

const string lib_decl = "decl @getint(): i32\n"\
                        "decl @getch(): i32\n"\
                        "decl @getarray(*i32): i32\n"\
                        "decl @putint(i32)\n"\
                        "decl @putch(i32)\n"\
                        "decl @putarray(i32, *i32)\n"\
                        "decl @starttime()\n"\
                        "decl @stoptime()\n";
const string lib_func[8] = {"getint", "getch", "getarray", "putint", "putch", "putarray", "starttime", "stoptime"};

int Compute_len(V boundary){
    int ret = 1;
    for(int i = 0; i < boundary.size(); i++)
        ret *= boundary[i];
    return ret;
}

int Align(V boundary, int num_sum, int baseline){
    int len = boundary.size(), i = 1, j = len - 1;
    V tmp;
    tmp.push_back(baseline);
    for(; i < len; i++)
        tmp.push_back(tmp[i-1] * boundary[len - i - 1]);
    for(; j >= 0; j--)
        if(num_sum % tmp[j] == 0){
            if(j == len - 1) // 一上来没有数字就是初始化列表：直接从下一维开始
                return 1;
            break;
        }
    return len - 1 - j;
}

void CompleteInit(Nest init, V boundary, Ainf* tmp){
    int num_sum = 0, should_sum = Compute_len(boundary); // 已经初始化的个数；需要初始化的个数
    int baseline = boundary[boundary.size()-1]; // 最后一维
    auto list = init.struc;
    for(int i = 0; i < list.size(); i++){
        if(list[i].is_num){ // 是数字
            num_sum++;
            tmp->finish.push_back(list[i].num);
        }
        else{ // 新的初始化列表
            if(num_sum % baseline != 0){
                cout << "没对齐！！！" << endl;
                exit(1);
            }
            V new_boundary;
            new_boundary.assign(boundary.begin() + Align(boundary, num_sum, baseline), boundary.end());
            num_sum += Compute_len(new_boundary);
            CompleteInit(list[i], new_boundary, tmp);
        }
    }
    // 最后对齐我负责的boundary
    for(; num_sum < should_sum; num_sum++)
        tmp->finish.push_back(0);
    return;
}

void Area::insert(string def, Inf inf){
    table.insert(make_pair(def, inf));
}

void Area::merge(){
    for(auto it = para.begin(); it != para.end(); it++){
        table.insert(make_pair(*it, Inf{true, top->tag}));
    }
}

void Area::merge_array(){
    for(auto it = para_array.begin(); it != para_array.end(); it++){
        string ident = it->first;
        Pinf p;
        p.dim.assign((it->second).begin(), (it->second).end());
        p.val = top->tag;
        p.len = p.dim.size();
        func_array.insert(make_pair(ident, p));
    }
}

Inf Area::find(string def){
    Area* tmp = top;
    while(tmp){
        auto found = tmp->table.find(def);
        if(found != tmp->table.end())
            return found->second;
        tmp = tmp->father;
    }
    return Inf{false, 0};
}

Ainf* Area::find_array(string def){
    Area* tmp = top;
    while(tmp){
        auto found = tmp->array.find(def);
        if(found != tmp->array.end())
            return &(found->second);
        tmp = tmp->father;
    }
    return NULL;
}

Pinf* Area::find_func_array(string def){
    Area* tmp = top;
    while(tmp){
        auto found = tmp->func_array.find(def);
        if(found != tmp->func_array.end())
            return &(found->second);
        tmp = tmp->father;
    }
    return NULL;
}

int Area::find_category(string def){
    Area* tmp = top;
    while(tmp){
        if(tmp->table.find(def) != tmp->table.end())
            return c_num;
        if(tmp->array.find(def) != tmp->array.end())
            return c_array;
        if(tmp->func_array.find(def) != tmp->func_array.end())
            return c_func_array;
        tmp = tmp->father;
    }
    return 0;
}

int Compute_Op(int a, int b, string op){
    switch(op[0]){
        case '=': return a == b;
        case '!': return a != b;

        case '|':
            if(op[1] == '|')
                return a || b;
            return a | b;
        case '&': 
            if(op[1] == '&')
                return a && b;
            return a & b;

        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/': return a / b;
        case '%': return a % b;

        case '<':
            if(op[1] == '=')
                return a <= b;
            return a < b;
        case '>':
            if(op[1] == '=')
                return a >= b;
            return a > b;
        
        default: assert(false);
    }
}