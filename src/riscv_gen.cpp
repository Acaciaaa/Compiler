#include <cassert>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <fstream>
#include "koopa.h"
#include <unordered_map>
#include <map>
#include <algorithm>
using namespace std;

extern unordered_map<string, bool> ret_func;
extern string riscv_str;
extern string koopa_str;
class ris_ret {
  public:
    int val;
    bool reg;
    bool elemptr;
    string var;
  ris_ret(int v = 0, bool r = false, string str = "", bool addr = false){
      val = v; reg = r; elemptr = addr; var = str;
    }
};

// 函数声明
void Visit(const koopa_raw_program_t &program);
void Visit(const koopa_raw_slice_t &slice);
void Visit(const koopa_raw_function_t &func);
void Visit(const koopa_raw_basic_block_t &bb);
ris_ret Visit(const koopa_raw_value_t &value);

void Visit(const koopa_raw_global_alloc_t &gt, string name, koopa_raw_type_t ty);
void Visit(const koopa_raw_aggregate_t &at);
ris_ret Visit(const koopa_raw_binary_t &bi);
void Visit(const koopa_raw_jump_t &jt);
void Visit(const koopa_raw_branch_t &bt);
ris_ret Visit(const koopa_raw_get_elem_ptr_t &elem);
ris_ret Visit(const koopa_raw_get_ptr_t &ptr);
void Visit(const koopa_raw_store_t &st);
ris_ret Visit(const koopa_raw_load_t &lt);
void Visit(const koopa_raw_return_t &ret);
ris_ret Visit(const koopa_raw_call_t &c);
ris_ret Visit(const koopa_raw_func_arg_ref_t &ft);
int Visit(const koopa_raw_integer_t &val);

void gen_riscv(){
  // 解析字符串 str, 得到 Koopa IR 程序
  koopa_program_t program;
  koopa_error_code_t ret = koopa_parse_from_string(koopa_str.c_str(), &program);
  assert(ret == KOOPA_EC_SUCCESS);  // 确保解析时没有出错
  // 创建一个 raw program builder, 用来构建 raw program
  koopa_raw_program_builder_t builder = koopa_new_raw_program_builder();
  // 将 Koopa IR 程序转换为 raw program
  koopa_raw_program_t raw = koopa_build_raw_program(builder, program);
  // 释放 Koopa IR 程序占用的内存
  koopa_delete_program(program);

  Visit(raw);

  koopa_delete_raw_program_builder(builder);
}

/*
-----------------------------------------------------------
-------------------riscv生成具体语句相关--------------------
-----------------------------------------------------------
*/

#define Align(a) ((a+15) & ~15)
#define Scope(a) (-2048 <= a && a <= 2047)
const string reg[18] = {"x0", "t0", "t1", "t2", "t3", "t4", "t5", "t6",
                        "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
                        "sp", "ra"};

// 全局变量（包括数组）：在发现Global_alloc类型时插入
vector<string> global_var;
// 全局变量的大小：在发现Global_alloc类型时插入
unordered_map<string, koopa_raw_type_t> global_space;
// 局部变量的大小：per函数更新+在发现alloc类型时插入
unordered_map<int, koopa_raw_type_t> local_space;
// 局部变量的大小：per函数更新+在算SRA时插入
unordered_map<string, int> local_size;
bool is_global;
koopa_raw_type_t index_ty; // getelemptr到第几步了

int S = 0, R = 0, A = 0; // S = s + R + A
int offset = 0; // per函数更新
int bbs_name = 1; // 不更新
unordered_map<koopa_raw_basic_block_t, int> BBS; // 不更新

int reg_aval = 1; // 只对integer用 per指令更新
bool call_integer = false; // trick: call调用integer的时候用
bool global_integer = false; // trick: 全局变量调用integer的时候用
bool elemptr_integer = false; // trick: getelemptr调用integer的时候用

#define RetReg 8
#define SP 16
#define RA 17
#define T0 1
#define T1 2
#define T2 3
#define T3 4
#define T4 5

map<int, string> Rop = {{KOOPA_RBO_ADD, "add"}, {KOOPA_RBO_SUB, "sub"}, {KOOPA_RBO_MUL, "mul"}, {KOOPA_RBO_DIV, "div"}, {KOOPA_RBO_MOD, "rem"}, 
{KOOPA_RBO_LT, "slt"}, {KOOPA_RBO_GT, "sgt"}, {KOOPA_RBO_OR, "or"}, {KOOPA_RBO_AND, "and"}, {KOOPA_RBO_XOR, "xor"}};

int Compute_byte(const koopa_raw_type_t ty){
  if(ty->tag == KOOPA_RTT_INT32)
    return 4;
  if(ty->tag == KOOPA_RTT_ARRAY)
    return ty->data.array.len * Compute_byte(ty->data.array.base);
  cout << "Compute_byte error!!!" << endl;
  return 0;
}

int Compute_SRA(const koopa_raw_slice_t &bbs){
  int s = 0, r = 0, a = 0;
  assert(bbs.kind == KOOPA_RSIK_BASIC_BLOCK);
  for(size_t i = 0; i < bbs.len; ++i){
    auto insts = (reinterpret_cast<koopa_raw_basic_block_t>(bbs.buffer[i]))->insts;
    assert(insts.kind == KOOPA_RSIK_VALUE);
    for(size_t j = 0; j < insts.len; ++j){
      auto value = reinterpret_cast<koopa_raw_value_t>(insts.buffer[j]);
      
      auto tag = value->ty->tag;
      if(tag != KOOPA_RTT_UNIT)
        s += 4;

      // 数组的alloc要算出来数组大小
      if(value->kind.tag == KOOPA_RVT_ALLOC){
        string name = value->name;
        name.erase(0, 1);
        auto tmp = value->ty->data.pointer.base;
        int need_byte = 4;
        if(tmp->tag != KOOPA_RTT_POINTER)
          need_byte = Compute_byte(tmp);
        local_size.insert(make_pair(name, need_byte));
        s += need_byte;
      }
      
      auto call_kind = value->kind;
      if(call_kind.tag == KOOPA_RVT_CALL){
        r = 4;
        auto args = call_kind.data.call.args; // koopa_raw_slice_t
        a = max(a, int(args.len));
      }
    }
  }
  R = r;
  A = 4 * max(0, a-8);
  s = A + R + s; // 左s为总的 右s为局部变量用的栈空间
  //cout << " A:" << A << " R:" << R << " total:" << s << " S:" << Align(s) << endl;
  return Align(s);
}

void li(int to, int imm){
  riscv_str = riscv_str + "li " + reg[to] + ", " + to_string(imm) + "\n";
}

void addi(int imm){
  // 专为sp服务
  riscv_str = riscv_str + "addi " + reg[SP] + ", " + reg[SP] + ", " + to_string(imm) + "\n";
}

void add(int to, int from){
  riscv_str = riscv_str + "add " + reg[to] + ", " + reg[from] + ", " + reg[to] + "\n";
}

void load(int to, int imm){
  if(Scope(imm))
    riscv_str = riscv_str + "lw " + reg[to] + ", " + to_string(imm) + "(sp)\n"; // lw t0, 8(sp)
  else{
    li(to, imm); // li t0, 5000
    add(to, SP); // add t0, sp, t0
    riscv_str = riscv_str + "lw " + reg[to] + ", " + "0(" + reg[to] + ")\n"; // lw t0, 0(t0)
  }
}

void store(int from, int imm){
  if(Scope(imm))
    riscv_str = riscv_str + "sw " + reg[from] + ", " + to_string(imm) + "(sp)\n"; // sw t0, 8(sp)
  else{
    if(from == RA){
      li(T0, imm);
      add(T0, SP);
      riscv_str = riscv_str + "sw " + reg[from] + ", 0(t0)\n";
    }
    else{
      li(from+1, imm); // li t1, 5000
      add(from+1, SP); // add t1, sp, t1
      riscv_str = riscv_str + "sw " + reg[from] + ", 0(" + reg[from+1] + ")\n"; // sw t0, 0(t1)
    }
  }
}

int seqz(int r){
  riscv_str = riscv_str + "seqz " + reg[r] + ", " + reg[r] + "\n";
  return r;
}

int snez(int r){
  riscv_str = riscv_str + "snez " + reg[r] + ", " + reg[r] + "\n";
  return r;
}

ris_ret riscv_binary(ris_ret l, ris_ret r, int op){
  // 前期load + op操作 + 后期store
  // 即使l和r都是寄存器，也最多用掉t0和t1；然后lw最多用掉t2和t3
  int l_reg = T2, r_reg = T3;
  if(!l.reg)
    load(T2, l.val);
  else
    l_reg = l.val;
  if(!r.reg)
    load(T3, r.val);
  else
    r_reg = r.val;

  string same = " " + reg[T0] + ", " + reg[l_reg] + ", " + reg[r_reg] + "\n";
  switch(op){
    case KOOPA_RBO_EQ:
      riscv_str = riscv_str + Rop[KOOPA_RBO_XOR] + same;
      seqz(T0);
      break;

    case KOOPA_RBO_NOT_EQ:
      riscv_str = riscv_str + Rop[KOOPA_RBO_XOR] + same;
      snez(T0);
      break;

    case KOOPA_RBO_LE:
      riscv_str = riscv_str + Rop[KOOPA_RBO_GT] + same;
      seqz(T0);
      break;

    case KOOPA_RBO_GE:
      riscv_str = riscv_str + Rop[KOOPA_RBO_LT] + same;
      seqz(T0);
      break;

    default:
      riscv_str = riscv_str + Rop[op] + same;
      break;
  }

  store(T0, offset);
  offset += 4;
  return ris_ret{offset-4, false};
}

// per call指令更新
int arg_reg_now = RetReg;
int arg_offset = 0;
void pre_arg(ris_ret arg){
  if(arg_reg_now < RetReg + 8){
    if(arg.reg)
      riscv_str = riscv_str + "mv " + reg[arg_reg_now++] + ", " + reg[arg.val] + "\n";
    else
      load(arg_reg_now++, arg.val);
  }
  else{
    if(arg.reg){
      store(arg.val, arg_offset);
      arg_offset += 4;
    }
    else{
      load(T0, arg.val);
      store(T0, arg_offset);
      arg_offset += 4;
    }
  }
}

void prologue(){
  if(S){
    // prologue
    if(Scope(-S))
      addi(-S); // addi sp, sp, -8
    else{
      li(T0, -S); // li t0, -5000
      add(SP, T0); // add sp, t0, sp
    }
    if(R)
      store(RA, S-4);
  }
}
void epilogue(){
  if(S){
    // epilogue
    if(R)
      load(RA, S-4);
    if(Scope(S))
      addi(S); // addi sp, sp, -8
    else{
      li(T0, S); // li t0, 5000
      add(SP, T0); // add sp, t0, sp
    }
  }
}
/*
-----------------------------------------------------------
-------------------riscv生成具体语句相关--------------------
-----------------------------------------------------------
*/

// 访问 raw program
void Visit(const koopa_raw_program_t &program) {
  // 执行一些其他的必要操作
  // ...
  // 访问所有全局变量
  Visit(program.values);
  // 访问所有函数
  Visit(program.funcs);
}

// 访问 raw slice
void Visit(const koopa_raw_slice_t &slice) {
  for (size_t i = 0; i < slice.len; ++i) {
    auto ptr = slice.buffer[i];
    // 根据 slice 的 kind 决定将 ptr 视作何种元素
    switch (slice.kind) {
      case KOOPA_RSIK_FUNCTION:
        // 访问函数
        Visit(reinterpret_cast<koopa_raw_function_t>(ptr));
        break;
      case KOOPA_RSIK_BASIC_BLOCK:
        // 访问基本块
        Visit(reinterpret_cast<koopa_raw_basic_block_t>(ptr));
        break;
      case KOOPA_RSIK_VALUE:
        // 访问指令
        reg_aval = 1; // 每条指令清零一下积累
        //cout << riscv_str << endl << endl;
        Visit(reinterpret_cast<koopa_raw_value_t>(ptr));
        break;
      default:
        // 我们暂时不会遇到其他内容, 于是不对其做任何处理
        assert(false);
    }
  }
}

// 访问函数
void Visit(const koopa_raw_function_t &func) {
  // 跳过函数定义
  if(func->bbs.len == 0)
    return;

  // .text .global [name] \n [name]:
  string tmp_name = func->name;
  tmp_name.erase(tmp_name.begin());
  riscv_str = riscv_str + "\n.text\n.globl " + tmp_name + "\n" + tmp_name + ":\n";

  // compute S'
  local_size.clear();
  local_space.clear();
  S = Compute_SRA(func->bbs);
  prologue();

  offset = A;

  // 访问所有基本块
  Visit(func->bbs);
  // cout << "global_space:" << endl;
  // for(auto it = global_space.begin(); it != global_space.end(); it++)
  //   cout << it->first << " " << it->second << endl;
  // cout << "local_space:" << endl;
  // for(auto it = local_space.begin(); it != local_space.end(); it++)
  //   cout << it->first << " " << it->second << endl;

  // epilogue在ret指令那里生成
}

// 访问基本块
void Visit(const koopa_raw_basic_block_t &bb) {
  auto name = BBS.find(bb);
  if(name != BBS.end())
    riscv_str = riscv_str + "\nbbs" + to_string(name->second) + ":\n";
  
  // 访问所有指令
  Visit(bb->insts);
}

unordered_map<koopa_raw_value_t, ris_ret> Hash;

// 访问指令
ris_ret Visit(const koopa_raw_value_t &value) {
  //cout << riscv_str << endl << endl;
  // 从下一层反着指回来的直接给它栈地址
  if(Hash.find(value) != Hash.end())
    return Hash[value];

  const auto &kind = value->kind;
  switch (kind.tag) {
    case KOOPA_RVT_ALLOC:{ // 分配个栈
      string name = value->name;
      name.erase(0, 1);
      auto tmp = value->ty->data.pointer.base;
      if(tmp->tag == KOOPA_RTT_POINTER) // alloc *i32
        local_space.insert(make_pair(offset, tmp->data.pointer.base));
      else // alloc i32
        local_space.insert(make_pair(offset, tmp->data.array.base));
      offset += local_size[name];
      auto inf = ris_ret{offset-local_size[name], false};
      Hash.insert(make_pair(value, inf));
      return inf;
    }

    case KOOPA_RVT_GLOBAL_ALLOC:{
      string name = value->name;
      name.erase(0, 1);
      // 第一次初始化
      if(find(global_var.begin(), global_var.end(), name) == global_var.end())
        Visit(kind.data.global_alloc, name, value->ty);
      return ris_ret{0, false, name};
    }

    case KOOPA_RVT_GET_ELEM_PTR:{
      ris_ret ret = Visit(kind.data.get_elem_ptr);
      Hash.insert(make_pair(value, ret));
      return ret;
    }

    case KOOPA_RVT_GET_PTR:{
      ris_ret ret = Visit(kind.data.get_ptr);
      Hash.insert(make_pair(value, ret));
      return ret;
    }

    case KOOPA_RVT_AGGREGATE:{
      Visit(kind.data.aggregate);
      return ris_ret{-1};
    }

    case KOOPA_RVT_ZERO_INIT:
      return ris_ret{0};

    case KOOPA_RVT_STORE: // 无返回 没人指我
      Visit(kind.data.store);
      break;

    case KOOPA_RVT_LOAD:{ // 返回ris_ret
      ris_ret ret = Visit(kind.data.load);
      Hash.insert(make_pair(value, ret));
      return ret;
    }

    case KOOPA_RVT_RETURN: // 无返回 没人指我
      Visit(kind.data.ret);
      break;

    case KOOPA_RVT_INTEGER:{ // 整数返回的是寄存器int
      int ret = Visit(kind.data.integer); 
      // 平时返回的是寄存器 全局init、elemptr找index返回的是数值
      if(global_integer || elemptr_integer)
        return ris_ret{ret, true};
      // 不太对劲，感觉不用insert到Hash
      if(ret != 0)
        Hash.insert(make_pair(value, ris_ret{ret, true}));
      return ris_ret{ret, true};
    }

    case KOOPA_RVT_BINARY:{ // 返回ris_ret
      ris_ret ret = Visit(kind.data.binary);
      Hash.insert(make_pair(value, ret));
      return ret;
    }

    case KOOPA_RVT_BRANCH: // 无返回 没人指我
      Visit(kind.data.branch);
      break;

    case KOOPA_RVT_JUMP: // 无返回 没人指我
      Visit(kind.data.jump);
      break;

    case KOOPA_RVT_CALL:{ // 返回ris_ret 其实就是a0
      ris_ret ret = Visit(kind.data.call);
      Hash.insert(make_pair(value, ret));
      return ret;
    }

    case KOOPA_RVT_FUNC_ARG_REF:{ // 返回ris_ret
      ris_ret ret = Visit(kind.data.func_arg_ref);
      Hash.insert(make_pair(value, ret));
      return ret;
    }

    default:
      cout << "新类型:" << kind.tag << endl;
      assert(false);
  }
  return 0; // useless
}

// 访问对应类型指令的函数定义略
// 视需求自行实现
// ...
void Visit(const koopa_raw_global_alloc_t &gt, string name, koopa_raw_type_t ty){
  global_var.push_back(name);
  global_space.insert(make_pair(name, ty));
  riscv_str = riscv_str + "\n.data\n.global " + name + "\n" + name + ":\n";
  global_integer = true;
  auto initval = Visit(gt.init).val; // 如果是aggregate就已经把.word输出完了，不用再管了
  global_integer = false;
  
  int need_byte = Compute_byte(ty->data.pointer.base); // 为了输出.zero sizeof()
  // zeroinit
  if(initval == 0)
    riscv_str = riscv_str + ".zero " + to_string(need_byte) + "\n";
  else if(initval != -1)
    riscv_str = riscv_str + ".word " + to_string(initval) + "\n";
  return;
}

void Visit(const koopa_raw_aggregate_t &at){
  auto slice = at.elems;
  assert(slice.kind == KOOPA_RSIK_VALUE);
  for (size_t i = 0; i < slice.len; ++i) {
    auto value = reinterpret_cast<koopa_raw_value_t>(slice.buffer[i]);
    auto initval = Visit(value).val;
    if(initval != -1) // 下面是int不是aggregate
      riscv_str = riscv_str + ".word " + to_string(initval) + "\n";
  }
}

void Visit(const koopa_raw_jump_t &jt){
  auto tar = jt.target;
  if(BBS.find(tar) == BBS.end())
    BBS.insert(make_pair(tar, bbs_name++));
  riscv_str = riscv_str + "j bbs" + to_string(BBS.find(tar)->second) + "\n";
}

void Visit(const koopa_raw_branch_t &bt){
  ris_ret cond = Visit(bt.cond);
  auto bb1 = bt.true_bb, bb2 = bt.false_bb;
  if(BBS.find(bb1) == BBS.end())
    BBS.insert(make_pair(bb1, bbs_name++));
  if(BBS.find(bb2) == BBS.end())
    BBS.insert(make_pair(bb2, bbs_name++));

  if(cond.reg)
    riscv_str = riscv_str + "bnez " + reg[cond.val] + ", bbs" + to_string(BBS.find(bb1)->second) + "\n";
  else{
    load(T0, cond.val);
    riscv_str = riscv_str + "bnez t0, bbs" + to_string(BBS.find(bb1)->second) + "\n";
  }
  riscv_str = riscv_str + "j bbs" + to_string(BBS.find(bb2)->second) + "\n";
}

ris_ret Visit(const koopa_raw_get_elem_ptr_t &elem){
  ris_ret from = Visit(elem.src);
  if(from.var != ""){ // 全局变量
    // ***
    is_global = true;
    index_ty = global_space[from.var]->data.pointer.base->data.array.base;

    riscv_str = riscv_str + "la t0, " + from.var + "\n";

    // getelemptr @arr, 2       getelemptr @arr, %7
    elemptr_integer = true;
    ris_ret index = Visit(elem.index);
    elemptr_integer = false;
    if(index.reg) // index是2
      riscv_str = riscv_str + "li " + reg[T1] + ", " + to_string(index.val) + "\n";
    else // index是%7
      load(T1, index.val);

    int need_byte = Compute_byte(index_ty);
    riscv_str = riscv_str + "li t2, " + to_string(need_byte) + "\n";
    index_ty = index_ty->data.array.base; // 更新

    riscv_str = riscv_str + "mul t1, t1, t2\n";

    add(T0, T1);

    store(T0, offset);
    offset += 4;
    return ris_ret{offset-4, false, "", true};
  }
  else{ // 局部变量或指针
    int imm = from.val;
    // ***
    if(local_space.find(imm) != local_space.end()){
      is_global = false;
      index_ty = local_space[imm];
    }

    if(Scope(imm)){
      if(from.elemptr)
        riscv_str = riscv_str + "lw t0, " + to_string(imm) + "(sp)\n"; // lw t0, 4(sp)
      else
        riscv_str = riscv_str + "addi t0, sp, " + to_string(imm) + "\n"; // addi t0, sp, 4
    }
    else{
      li(T0, imm); // li t0, 5000
      add(T0, SP); // add t0, sp, t0
      if(from.elemptr)
        riscv_str += "lw t0, 0(t0)\n";
    }

    // getelemptr @arr, 2       getelemptr @arr, %7
    elemptr_integer = true;
    ris_ret index = Visit(elem.index);
    elemptr_integer = false;
    if(index.reg) // index是2
      riscv_str = riscv_str + "li " + reg[T1] + ", " + to_string(index.val) + "\n";
    else // index是%7
      load(T1, index.val);

    int need_byte = Compute_byte(index_ty);
    riscv_str = riscv_str + "li t2, " + to_string(need_byte) + "\n";
    // 更新
    if(is_global) // 是全局变量过来的
      index_ty = index_ty->data.array.base;
    else // 自始至终是局部变量
      local_space.insert(make_pair(offset, index_ty->data.array.base));

    riscv_str = riscv_str + "mul t1, t1, t2\n";

    add(T0, T1);

    store(T0, offset);
    offset += 4;
    return ris_ret{offset-4, false, "", true};
  }
}

ris_ret Visit(const koopa_raw_get_ptr_t &ptr){
  ris_ret from = Visit(ptr.src);
  int imm = from.val;
  // ***
  if(local_space.find(imm) != local_space.end()){
    is_global = false;
    index_ty = local_space[imm];
  }
  if(!from.elemptr) // getptr之后一定是指针
    cout << "getptr不可能碰到" << endl;

  if(Scope(imm)){
    riscv_str = riscv_str + "lw t0, " + to_string(imm) + "(sp)\n"; // lw t0, 4(sp)
  }
  else{
    li(T0, imm); // li t0, 5000
    add(T0, SP); // add t0, sp, t0
    riscv_str += "lw t0, 0(t0)\n";
  }

  // getptr %3, 1
  elemptr_integer = true;
  ris_ret index = Visit(ptr.index);
  elemptr_integer = false;
  if(index.reg) // index是2
    riscv_str = riscv_str + "li " + reg[T1] + ", " + to_string(index.val) + "\n";
  else // index是%7
    load(T1, index.val);

  int need_byte = Compute_byte(index_ty);
  riscv_str = riscv_str + "li t2, " + to_string(need_byte) + "\n";
  // 不可能从全局变量过来
  // 自始至终是局部变量
  local_space.insert(make_pair(offset, index_ty->data.array.base));

  riscv_str = riscv_str + "mul t1, t1, t2\n";

  add(T0, T1);

  store(T0, offset);
  offset += 4;
  return ris_ret{offset-4, false, "", true};
}

void Visit(const koopa_raw_store_t &st){
  ris_ret from = Visit(st.value), to = Visit(st.dest);
  //cout << "from.reg:" << from.reg << " from.val:" << from.val << endl;
  if(from.reg){
    if(to.var != ""){
      //cout << "寄存器+全局" << endl;
      riscv_str = riscv_str + "la " + reg[T4] + ", " + to.var + "\n";
      riscv_str = riscv_str + "sw " + reg[from.val] + ", 0(t4)\n";
    }
    else if(to.elemptr){
      //寄存器+数组
      load(T4, to.val); // 前面移立即数会占用寄存器用T4保险一些
      riscv_str = riscv_str + "sw " + reg[from.val] + ", 0(t4)\n";
    }
    else{
      //cout << "寄存器+局部" << endl;
      store(from.val, to.val);
    }
  }
  else{
    if(to.var != ""){
      //cout << "内存+全局" << endl;
      riscv_str = riscv_str + "la " + reg[T4] + ", " + to.var + "\n";
      load(T0, from.val);
      riscv_str = riscv_str + "sw t0, 0(t4)\n";
    }
    else if(to.elemptr){
      //内存+数组
      load(T0, from.val);
      load(T1, to.val);
      riscv_str += "sw t0, 0(t1)\n";
      //local_space.insert(make_pair(to.val, local_space[from.val]));
    }
    else{
      //cout << "内存+局部" << endl;
      load(T0, from.val);
      store(T0, to.val);
    }
  }
}

ris_ret Visit(const koopa_raw_load_t &lt){
  ris_ret from = Visit(lt.src);
  // 全局变量
  if(from.var != "")
    riscv_str = riscv_str + "la t0, " + from.var + "\nlw t0, 0(t0)\n";
  else if(from.elemptr){
    load(T0, from.val);
    riscv_str = riscv_str + "lw t0, 0(t0)\n";
  }
  else{
    if(local_space.find(from.val) != local_space.end()){
      local_space.insert(make_pair(offset, local_space[from.val]));
      load(T0, from.val);
      store(T0, offset);
      offset += 4;
      return ris_ret{offset-4, false, "", true};
    }
    load(T0, from.val);
  }
  store(T0, offset);
  offset += 4;
  return ris_ret{offset-4, false};
}

ris_ret Visit(const koopa_raw_binary_t &bi){
  switch(bi.op){
    case KOOPA_RBO_ADD:
    case KOOPA_RBO_SUB:
    case KOOPA_RBO_MUL:
    case KOOPA_RBO_DIV:
    case KOOPA_RBO_MOD:

    case KOOPA_RBO_OR:
    case KOOPA_RBO_AND:

    case KOOPA_RBO_EQ:
    case KOOPA_RBO_NOT_EQ:

    case KOOPA_RBO_LT:
    case KOOPA_RBO_GT:
    case KOOPA_RBO_LE:
    case KOOPA_RBO_GE:{
      ris_ret l = Visit(bi.lhs), r = Visit(bi.rhs);
      return riscv_binary(l, r, bi.op);
    }

    default:
      assert(false);
  }
}

void Visit(const koopa_raw_return_t &ret){
  const auto ret_val = ret.value;
  if(!ret_val){
    epilogue();
    riscv_str += "ret\n";
    return;
  }
  switch(ret_val->kind.tag){
    case KOOPA_RVT_INTEGER:
    case KOOPA_RVT_BINARY:
    case KOOPA_RVT_LOAD:{
      ris_ret i = Visit(ret_val);
      if(i.reg) // mv a0, t2
        riscv_str = riscv_str + "mv a0, " + reg[i.val] + "\n";
      else
        load(RetReg, i.val); // lw a0, 12(sp)
      
      epilogue();
      riscv_str += "ret\n";
      break;
    }

    case KOOPA_RVT_CALL:{
      epilogue();
      riscv_str += "ret\n";
      break;
    }

    default:
      //cout << ret_val->kind.tag << endl;
      assert(false);
  }
}

ris_ret Visit(const koopa_raw_call_t &c){
  arg_reg_now = RetReg;
  arg_offset = 0;
  call_integer = true;

  auto args = c.args; // koopa_raw_slice_t
  assert(args.kind == KOOPA_RSIK_VALUE);
  for (size_t i = 0; i < args.len; ++i) {
    auto arg_inst = reinterpret_cast<koopa_raw_value_t>(args.buffer[i]);
    ris_ret arg = Visit(arg_inst);
    pre_arg(arg);
  }
  string func_name = c.callee->name;
  func_name.erase(0, 1);
  riscv_str = riscv_str + "call " + func_name + "\n";
  
  call_integer = false;
  // 能接住
  if(ret_func[func_name]){
    store(RetReg, offset);
    offset += 4;
    return ris_ret{offset-4, false};
  }
  return ris_ret{RetReg, true};
}

ris_ret Visit(const koopa_raw_func_arg_ref_t &ft){
  int which_arg = ft.index;
  if(which_arg < 8)
    return ris_ret{RetReg+which_arg, true};
  return ris_ret{S+4*(which_arg-8), false};
}

int Visit(const koopa_raw_integer_t &val){
  int32_t int_val = val.value;
  if (int_val == 0)
    return 0;
  if(global_integer || elemptr_integer)
    return int_val;
  if(call_integer){
    li(T0, int_val);
    return T0;
  }
  li(reg_aval, int_val);
  reg_aval++;
  return reg_aval-1;
}
