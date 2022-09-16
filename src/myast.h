#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <stack>
using namespace std;

#define Valid(p) (!(p == NULL))
#define V vector<int>
#define c_num 1
#define c_array 2
#define c_func_array 3
class Nest{
  public:
  bool is_num;
  int num;
  vector<Nest> struc;
};

extern string koopa_str;
extern int koopa_reg;

typedef struct{
  bool var; // true->var false->const
  int val; // const_val或val_tag
}Inf;
typedef struct{
  int val;
  int len; // a[2][4][3]: 3维
  V finish; // 补全0以后的值
  bool is_const;
  //vector<int> value;
}Ainf;
typedef struct{
  int val;
  int len; // a[]: 0维 a[][10]: 1维
  V dim;
}Pinf;
class Area {
  public:
  Area* father = NULL;
  unordered_map<string, Inf> table;
  int tag = 0;
  unordered_map<string, Ainf> array;
  unordered_map<string, Pinf> func_array;

  void insert(string def, Inf inf);
  void merge();
  void merge_array();
  Inf find(string def);
  Ainf* find_array(string def);
  Pinf* find_func_array(string def);
  int find_category(string def);
  // 三个操作
  // 不清楚delete能不能释放掉哈希表 暂存疑惑
};
extern void CompleteInit(Nest init, V boundary, Ainf* tmp);
extern Area* top; // 当前符号表
extern vector<string> para; // 当前形参表
extern unordered_map<string, V> para_array; // 当前形参表：数组
extern int tag; // 作用域编号: per函数更新
extern bool whether_load;
extern unordered_map<string, bool> ret_func; // 要不要用%接函数返回值
extern const string lib_decl;
extern const string lib_func[8];
int Compute_Op(int a, int b, string op);

class koo_ret {
  public:
    int val;
    bool reg;
    koo_ret(int v = 0, bool r = false){
      val = v; reg = r;
    }
};
string Load(string ident);
void Store(string ident, string from);
void Alloc(string ident);
void Global(string ident, int initval);
void GlobalArray(string ident, V init, V boundary);
void AllocArray(string ident, V init, V boundary);
void Getelemptr(string ident, V boundary);
string Getelemptr_only(string ident, vector<string> offset);
koo_ret koo_parse(string str);
string koo_binary(koo_ret l, koo_ret r, string op);

// 所有 AST 的基类
class BaseAST {
 public:
  virtual ~BaseAST() = default;
  
  virtual void Dump() const = 0;
  virtual string Koo(BaseAST* next = NULL) const = 0;
  virtual int Compute(){return 0;}
  virtual Nest ComputeArray(){return Nest{};}
};

extern unordered_map<BaseAST*, int> bbs_begin;
extern unordered_map<int, int> bbs_end;
extern stack<int> while_entry;
extern stack<int> while_end;
extern int bbs_num;
extern int bbs_now;
bool bbs_begin_exit(BaseAST* ptr);
void bbs_begin_insert(BaseAST* ptr, int num);
void print_bbs_begin(int num);
void Bind(int begin, int end);
void Inherit(int predecessor, int successor);
void Branch(string cond, int then_bbs, int else_bbs, int end);
void Branch_(string cond, int then_bbs, int end);
void Jump(int bbs);

// 全局作用域
class SAST : public BaseAST{
  public:
  unique_ptr<BaseAST> compUnit;

  void Dump() const override{
    cout << "SAST {";
    compUnit->Dump();
    cout << " }";
  }

  string Koo(BaseAST* next) const override {
    top = new Area();
    top->father = NULL;
    top->tag = 0;
    // 库函数
    koopa_str += lib_decl;
    for(int i = 0; i < 8; i++){
      top->insert(lib_func[i], Inf{true, top->tag});
      if(i < 3)
        ret_func.insert(make_pair(lib_func[i], true));
      else 
        ret_func.insert(make_pair(lib_func[i], false));
    }

    compUnit->Koo();

    delete top;
    return "";
  }
};

// 唯一一个|没有分开的AST
class CompUnitAST : public BaseAST {
 public:
  BaseAST* compUnit;
  unique_ptr<BaseAST> funcDef;
  unique_ptr<BaseAST> decl;
  bool func;

  void Dump() const override {
    cout << "CompUnitAST { ";
    if(Valid(compUnit)){
      compUnit->Dump();
      cout << ", ";
    }
    if(func)
      funcDef->Dump();
    else
      decl->Dump();
    cout << " }";
  }

  string Koo(BaseAST* next) const override {
    if(Valid(compUnit))
      compUnit->Koo();
    if(func)
      return funcDef->Koo();
    return decl->Koo();
  }
};

// 这里加入了bbs_end(0, 0);
class FuncDefAST : public BaseAST {
 public:
  unique_ptr<BaseAST> funcType;
  string ident;
  BaseAST* funcFParams;
  unique_ptr<BaseAST> block;

  void Dump() const override {
    cout << "FuncDefAST { ";
    funcType->Dump();
    cout << ", " << ident << ", ";
    if(Valid(funcFParams)){
      funcFParams->Dump();
      cout << ", ";
    }
    block->Dump();
    cout << " }";
  }

  string Koo(BaseAST* next) const override {
    // 新的函数 清空bbs_begin bbs_end bbs_num bbs_now tag重新开始编号
    bbs_begin.clear();
    bbs_end.clear();
    bbs_num = 1;
    bbs_now = 0;
    tag = 1;

    // 把当前基本块(最原始的、每个函数中的 0基本块)加到bbs_end中
    bbs_end.insert(make_pair(bbs_now, 0));
    
    // 把函数加到全局作用域里
    top->insert(ident, Inf{true, top->tag});

    // 打印出函数的koopa
    koopa_str = koopa_str + "\nfun @" + ident + "(";
    if(Valid(funcFParams))
      funcFParams->Koo();
    koopa_str += ")";
    string type = funcType->Koo();
    // 为了以后判断call要不要用寄存器来接
    if(type[0] == 'v')
      ret_func.insert(make_pair(ident, false));
    else 
      ret_func.insert(make_pair(ident, true));
    koopa_str += " {\n\%entry:\n";

    // 给参数重新分配一次空间：参数% 重分@
    for(auto it = para.begin(); it != para.end(); it++){
      if(tag != 1)
        cout << "mamaya!!!" << endl;
      string name = *it + "1";
      Alloc(name);
      Store(name, "%" + name);
    }
    for(auto it = para_array.begin(); it != para_array.end(); it++){
      string name = it->first + "1";

      koopa_str = koopa_str + "@" + name + " = alloc *";
      int dim_num = it->second.size();
      for(int i = 0; i < dim_num; i++)
        koopa_str += "[";
      koopa_str += "i32";
      for(int i = dim_num-1; i >= 0; i--)
        koopa_str = koopa_str + ", " + to_string(it->second[i]) + "]";
      koopa_str += "\n";

      Store(name, "%" + name);
    }

    block->Koo();

    // 这个函数是个烂尾：
    // 当前基本块没输出的先输出了+有不是-1的基本块也输出了
    if(bbs_end[bbs_now] > 0){
      Jump(bbs_now);
    }
    else if(bbs_end[bbs_now] == 0){
      koopa_str += "ret\n";
      bbs_end[bbs_now] = -1;
    }
    for(auto it = bbs_end.begin(); it != bbs_end.end(); it++){
      if(it->second != -1){
        if(it->second != 0)
          cout << "i hate bbs:" << it->second << endl;
        print_bbs_begin(it->first);
        koopa_str += "ret\n";
      }
    }
    
    koopa_str += " }\n";
    para.clear();
    para_array.clear();
    return "";
  }
};

class FuncFParamsAST : public BaseAST{
  public:
  vector<BaseAST*> funcFParam_;

  void Dump() const override {
    cout << "FuncFParamsAST { ";
    for(int i = 0; i < funcFParam_.size(); i++)
      funcFParam_[i]->Dump();
    cout << " }";
  }

  string Koo(BaseAST* next) const override{
    int len = funcFParam_.size();
    for(int i = 0; i < len; i++){
      funcFParam_[i]->Koo();
      if(i != len-1){
        koopa_str += ", ";
      }
    }
    return "";
  }
};

class FuncFParamIntAST : public BaseAST{
  public:
  unique_ptr<BaseAST> bType;
  string ident;

  void Dump() const override{
    cout << "FuncFParamIntAST { ";
    bType->Dump();
    cout << ", " << ident << " }";
  }

  string Koo(BaseAST* next) const override{
    // 加进单独符号表里
    para.push_back(ident);

    koopa_str = koopa_str + "%" + ident + "1";
    return bType->Koo();
  }
};

class FuncFParamArrayAST : public BaseAST{
  public:
  unique_ptr<BaseAST> bType;
  string ident;
  vector<BaseAST*> constExp_;

  void Dump() const override{
    cout << "FuncFParamArrayAST { ";
    bType->Dump();
    cout << ", " << ident << " }";
  }

  string Koo(BaseAST* next) const override{
    V tmp;
    int dim_num = constExp_.size();
    for(int i = 0; i < dim_num; i++)
      tmp.push_back(constExp_[i]->Compute());

    koopa_str = koopa_str + "%" + ident + "1: *";
    for(int i = 0; i < dim_num; i++)
      koopa_str += "[";
    koopa_str += "i32";
    for(int i = dim_num-1; i >= 0; i--)
      koopa_str = koopa_str + ", " + to_string(tmp[i]) + "]";
    
    // 加进单独符号表里
    para_array.insert(make_pair(ident, tmp));

    return "";
  }
};

// 新的作用域（语句块）
class BlockAST : public BaseAST {
    public:
  vector<BaseAST*> blockItem_;

  void Dump() const override {
    cout << "BlockAST { ";
    for(int i = 0; i < blockItem_.size(); i++)
      blockItem_[i]->Dump();
    cout << " }";
  }

  string Koo(BaseAST* next) const override {
    // 当前符号表更新
    Area* tmp = top;
    top = new Area();
    top->father = tmp;
    top->tag = tag++;
    // 如果是一个函数新开始的作用域，就把函数参数的para和para_array加进去
    if(top->tag == 1){
      top->merge();
      top->merge_array();
    }

    int len = blockItem_.size();
    for(int i = 0; i < len; i++){
      // 走到新的基本块了：打印之前基本块的末尾+切换bbs_now+打印基本块入口
      if(bbs_begin_exit(blockItem_[i])){
        Jump(bbs_now);
        bbs_now = bbs_begin.find(blockItem_[i])->second;
        print_bbs_begin(bbs_now);
      }
      else{
        if(bbs_end[bbs_now] < 0)
          continue;
      }

      if(i < len-1){
        blockItem_[i]->Koo(blockItem_[i+1]);
      }
      else{
        blockItem_[i]->Koo(next);
      }
    }

    // 恢复当前符号表
    tmp = top;
    top = top->father;
    delete tmp;

    return "";
  }
};

class BlockItemToDeclAST : public BaseAST {
  public:
  unique_ptr<BaseAST> decl;

  void Dump() const override {
    cout << "BlockItemToDeclAST { ";
    decl->Dump();
    cout << " }";
  }

  string Koo(BaseAST* next) const override {
    decl->Koo(next);
    return "";
  }
};

class BlockItemToStmtAST : public BaseAST {
  public:
  unique_ptr<BaseAST> stmt;

  void Dump() const override {
    cout << "BlockItemToStmtAST { ";
    stmt->Dump();
    cout << " }";
  }

  string Koo(BaseAST* next) const override {
    return stmt->Koo(next);
  }
};

class DeclToConstAST : public BaseAST{
  public:
  unique_ptr<BaseAST> constDecl;

  void Dump() const override {
    cout << "DeclToConstAST { ";
    constDecl->Dump();
    cout << " }";
  }

  string Koo(BaseAST* next) const override {
    return constDecl->Koo();
  }
};

class DeclToVarAST : public BaseAST{
  public:
  unique_ptr<BaseAST> varDecl;

  void Dump() const override {
    cout << "DeclToVarAST { ";
    varDecl->Dump();
    cout << " }";
  }

  string Koo(BaseAST* next) const override {
    return varDecl->Koo(next);
  }
};

class ConstDeclAST : public BaseAST{
  public:
  unique_ptr<BaseAST> bType;
  vector<BaseAST*> constDef_;

  void Dump() const override {
    cout << "ConstDeclAST { ";
    cout << "const, ";
    bType->Dump();
    cout << ", ";
    for(int i = 0; i < constDef_.size(); i++)
      constDef_[i]->Dump();
    cout << " }";
  }

  string Koo(BaseAST* next) const override {
    for(int i = 0; i < constDef_.size(); i++)
      constDef_[i]->Koo();
    return "";
  }
};

class VarDeclAST : public BaseAST{
  public:
  unique_ptr<BaseAST> bType;
  vector<BaseAST*> varDef_;

  void Dump() const override {
    cout << "VarDeclAST { ";
    bType->Dump();
    cout << ", ";
    for(int i = 0; i < varDef_.size(); i++)
      varDef_[i]->Dump();
    cout << " }";
  }

  string Koo(BaseAST* next) const override {
    for(int i = 0; i < varDef_.size(); i++)
      varDef_[i]->Koo(next);
    return "";
  }
};

class BTypeAST : public BaseAST{
  public:
  string type;

  void Dump() const override {
    cout << "BTypeAST { " << type << " }";
  }

  string Koo(BaseAST* next) const override {
    if(type[0] == 'i')
      koopa_str += ": i32";
    return type;
  }
};

class ConstDefAST : public BaseAST{
  public:
  string ident;
  vector<BaseAST*> constExp_;
  unique_ptr<BaseAST> constInitVal;

  void Dump() const override {
    cout << "ConstDefAST { " << ident << ", ";
    for(int i = 0; i < constExp_.size(); i++)
      constExp_[i]->Dump();
    cout << ", ";
    constInitVal->Dump();
    cout << " }";
  }

  string Koo(BaseAST* next) const override {
    int brace = constExp_.size();
    if(brace != 0){ // 数组
      V boundary;
      for(int i = 0; i < brace; i++)
        boundary.push_back(constExp_[i]->Compute());
      Ainf tmp;
      tmp.val = top->tag;
      tmp.len = brace;
      tmp.is_const = true;

      Nest init = constInitVal->ComputeArray();
      if(init.struc.size() != 0)
        CompleteInit(init, boundary, &tmp);
      top->array.insert(make_pair(ident, tmp));

      // 全局常数组
      if(top->tag == 0)
        GlobalArray(ident, tmp.finish, boundary);
      else
        AllocArray(ident + to_string(top->tag), tmp.finish, boundary);
    }
    else{ // 单值
      int val = constInitVal->Compute();
      top->insert(ident, Inf{false, val});
    }
    return "";
  }
};

class VarDefToOneAST : public BaseAST{
  public:
  string ident;
  vector<BaseAST*> constExp_;

  void Dump() const override {
    cout << "VarDefToOneAST { " << ident;
    for(int i = 0; i < constExp_.size(); i++)
      constExp_[i]->Dump();
    cout << " }";
  }

  string Koo(BaseAST* next) const override {
    int brace = constExp_.size();
    if(brace != 0){ // 数组
      V boundary;
      for(int i = 0; i < brace; i++)
        boundary.push_back(constExp_[i]->Compute());
      Ainf tmp;
      tmp.val = top->tag;
      tmp.len = brace;
      tmp.is_const = false;

      Nest init; init.is_num = false; // 这玩意儿是空的
      top->array.insert(make_pair(ident, tmp));

      // 全局数组
      if(top->tag == 0)
        GlobalArray(ident, tmp.finish, boundary);
      else
        AllocArray(ident + to_string(top->tag), tmp.finish, boundary);
    }
    else{ // 单值
      top->insert(ident, Inf{true, top->tag});
      // 全局变量
      if(top->tag == 0)
        Global(ident, 0);
      else
        Alloc(ident + to_string(top->tag));
    }
    return "";
  }
};

class VarDefToTwoAST : public BaseAST{
  public:
  string ident;
  vector<BaseAST*> constExp_;
  unique_ptr<BaseAST> initVal;
  
  void Dump() const override {
    cout << "VarDefToTwoAST { " << ident << ", ";
    for(int i = 0; i < constExp_.size(); i++)
      constExp_[i]->Dump();
    cout << ", ";
    initVal->Dump();
    cout << " }";
  }

  string Koo(BaseAST* next) const override {
    int brace = constExp_.size();
    if(brace != 0){ // 数组
      V boundary;
      for(int i = 0; i < brace; i++)
        boundary.push_back(constExp_[i]->Compute());
      Ainf tmp;
      tmp.val = top->tag;
      tmp.len = brace;
      tmp.is_const = false;

      Nest init = initVal->ComputeArray();
      if(init.struc.size() != 0)
        CompleteInit(init, boundary, &tmp);
      top->array.insert(make_pair(ident, tmp));

      // 全局数组
      if(top->tag == 0)
        GlobalArray(ident, tmp.finish, boundary);
      else
        AllocArray(ident + to_string(top->tag), tmp.finish, boundary);
    }
    else{
      top->insert(ident, Inf{true, top->tag});
      // 全局变量
      if(top->tag == 0){
        auto initval = initVal->Compute();
        Global(ident, initval);
      }
      else{
        Alloc(ident + to_string(top->tag));
        string from = initVal->Koo(next);
        Store(ident + to_string(top->tag), from);
      }
    }
    return "";
  }
};

class ConstInitValToOneAST : public BaseAST{
  public:
  unique_ptr<BaseAST> constExp;

  void Dump() const override {
    cout << "ConstInitValToOneAST { ";
    constExp->Dump();
    cout << " }";
  }

  Nest ComputeArray() override{
    Nest tmp; tmp.is_num = true; tmp.num = constExp->Compute();
    return tmp;
  }

  int Compute() override {
    // 一个值直接返回
    return constExp->Compute();
  }

  string Koo(BaseAST* next) const override {
    return "";
  }
};

class ConstInitValToArrayAST : public BaseAST{
  public:
  vector<BaseAST*> constInitVal_;

  void Dump() const override {
    cout << "ConstInitValToArrayAST { ";
    for(int i = 0; i < constInitVal_.size(); i++)
      constInitVal_[i]->Dump();
    cout << " }";
  }

  Nest ComputeArray() override{
    Nest n;
    n.is_num = false;
    for(int i = 0; i < constInitVal_.size(); i++){
      Nest tmp = constInitVal_[i]->ComputeArray();
      n.struc.push_back(tmp);
    }
    return n;
  }

  string Koo(BaseAST* next) const override {return "";}
};

class InitValToOneAST : public BaseAST{
  public:
  unique_ptr<BaseAST> exp;

  void Dump() const override {
    cout << "InitValToOneAST { ";
    exp->Dump();
    cout << " }";
  }

  Nest ComputeArray() override{
    Nest tmp; tmp.is_num = true; tmp.num = exp->Compute();
    return tmp;
  }

  int Compute() override {
    return exp->Compute();
  }

  string Koo(BaseAST* next) const override {
    return exp->Koo(next);
  }
};

class InitValToArrayAST : public BaseAST{
  public:
  vector<BaseAST*> initVal_;

  void Dump() const override {
    cout << "InitValToArrayAST { ";
    for(int i = 0; i < initVal_.size(); i++)
      initVal_[i]->Dump();
    cout << " }";
  }

  Nest ComputeArray() override{
    Nest n;
    n.is_num = false;
    for(int i = 0; i < initVal_.size(); i++){
      Nest tmp = initVal_[i]->ComputeArray();
      n.struc.push_back(tmp);
    }
    return n;
  }

  string Koo(BaseAST* next) const override {return "";}
};

class ConstExpAST : public BaseAST{
  public:
  unique_ptr<BaseAST> exp;

  void Dump() const override {
    cout << "ConstExpAST { ";
    exp->Dump();
    cout << " }";
  }

  int Compute() override {
    return exp->Compute();
  }

  string Koo(BaseAST* next) const override {
    return "";
  }
};

class StmtToMatchedAST : public BaseAST{
  public:
  unique_ptr<BaseAST> matched;

  void Dump() const override {
    cout << "StmtToMatchedAST { ";
    matched->Dump();
    cout << " }";
  }

  string Koo(BaseAST* next) const override {
    return matched->Koo(next);
  }
};

class MatchedToIfAST : public BaseAST{
  public:
  unique_ptr<BaseAST> exp;
  unique_ptr<BaseAST> matched1;
  unique_ptr<BaseAST> matched2;

  void Dump() const override {
    cout << "MatchedToIfAST { if(";
    exp->Dump();
    cout << ") ";
    matched1->Dump();
    cout << " else:";
    matched2->Dump();
    cout << " }";
  }

  string Koo(BaseAST* next) const override {
    string cond = exp->Koo();

    int then_bbs = bbs_num, else_bbs = bbs_num + 1, next_bbs;
    bbs_num += 2;
    if(!bbs_begin_exit(next)){
      next_bbs = bbs_num++;
      bbs_begin_insert(next, next_bbs);
    }
    else{
      next_bbs = bbs_begin.find(next)->second;
    }
    Bind(then_bbs, next_bbs);
    Bind(else_bbs, next_bbs);
    // 更改当前基本块的结局，或许更改next_bbs的结局
    Inherit(bbs_now, next_bbs);
    
    Branch(cond, then_bbs, else_bbs, next_bbs);
    bbs_now = then_bbs; // 进入另一个基本块
    matched1->Koo(next);
    
    Jump(bbs_now);
    print_bbs_begin(else_bbs);
    bbs_now = else_bbs; // 进入另一个基本块
    matched2->Koo(next);
    return "";
  }
};

class MatchedToOtherAST : public BaseAST{
  public:
  unique_ptr<BaseAST> other;

  void Dump() const override {
    cout << "MatchedToOtherAST { ";
    other->Dump();
    cout << " }";
  }

  string Koo(BaseAST* next) const override {
    return other->Koo(next);
  }
};

class StmtToUnmatchedAST : public BaseAST{
  public:
  unique_ptr<BaseAST> unmatched;

  void Dump() const override {
    cout << "StmtToUnmatchedAST { if:";
    unmatched->Dump();
    cout << " }";
  }

  string Koo(BaseAST* next) const override {
    return unmatched->Koo(next);
  }
};

class UnmatchedToOneAST : public BaseAST{
  public:
  unique_ptr<BaseAST> exp;
  unique_ptr<BaseAST> stmt;

  void Dump() const override {
    cout << "UnmatchedToOneAST { if(";
    exp->Dump();
    cout << ") ";
    stmt->Dump();
    cout << " }";
  }

  string Koo(BaseAST* next) const override {
    string cond = exp->Koo();

    int then_bbs = bbs_num++, next_bbs;
    if(!bbs_begin_exit(next)){
      next_bbs = bbs_num++;
      bbs_begin_insert(next, next_bbs);
    }
    else{
      next_bbs = bbs_begin.find(next)->second;
    }
    Bind(then_bbs, next_bbs);
    // 更改当前基本块的结局，或许更改next_bbs的结局
    Inherit(bbs_now, next_bbs);

    Branch_(cond, then_bbs, next_bbs);
    bbs_now = then_bbs; // 进入另一个基本块
    stmt->Koo(next);
    return "";
  }
};

class UnmatchedToTwoAST : public BaseAST{
  public:
  unique_ptr<BaseAST> exp;
  unique_ptr<BaseAST> matched;
  unique_ptr<BaseAST> unmatched;

  void Dump() const override {
    cout << "UnmatchedToTwoAST { if(";
    exp->Dump();
    cout << ") ";
    matched->Dump();
    cout << " else:";
    unmatched->Dump();
    cout << " }";
  }

  string Koo(BaseAST* next) const override {
    string cond = exp->Koo();
    
    int then_bbs = bbs_num, else_bbs = bbs_num + 1, next_bbs;
    bbs_num += 2;
    if(!bbs_begin_exit(next)){
      next_bbs = bbs_num++;
      bbs_begin_insert(next, next_bbs);
    }
    else{
      next_bbs = bbs_begin.find(next)->second;
    }
    Bind(then_bbs, next_bbs);
    Bind(else_bbs, next_bbs);
    // 更改当前基本块的结局，或许更改next_bbs的结局
    Inherit(bbs_now, next_bbs);

    Branch(cond, then_bbs, else_bbs, next_bbs);
    bbs_now = then_bbs; // 进入另一个基本块
    matched->Koo(next);
    
    Jump(bbs_now);
    print_bbs_begin(else_bbs);
    bbs_now = else_bbs; // 进入另一个基本块
    unmatched->Koo(next);
    return "";
  }
};

class OtherToWhileAST : public BaseAST{
  public:
  unique_ptr<BaseAST> exp;
  unique_ptr<BaseAST> stmt;

  void Dump() const override {
    cout << "OtherToWhileAST { while(";
    exp->Dump();
    cout << ") ";
    stmt->Dump();
    cout << " }";
  }

  string Koo(BaseAST* next) const override{
    int entry_bbs = bbs_num, body_bbs = bbs_num + 1, next_bbs;
    bbs_num += 2;
    if(!bbs_begin_exit(next)){
      next_bbs = bbs_num++;
      bbs_begin_insert(next, next_bbs);
    }
    else{
      next_bbs = bbs_begin.find(next)->second;
    }
    while_entry.push(entry_bbs);
    while_end.push(next_bbs);
    bbs_begin_insert((BaseAST*)(this), entry_bbs);
    bbs_end.insert(make_pair(entry_bbs, -1));
    Bind(body_bbs, entry_bbs);
    Inherit(bbs_now, next_bbs);
    
    koopa_str = koopa_str + "jump %_" + to_string(entry_bbs) + "\n";
    
    bbs_now = entry_bbs;
    print_bbs_begin(entry_bbs);
    string cond = exp->Koo();
    Branch_(cond, body_bbs, next_bbs);

    bbs_now = body_bbs; // 进入另一个基本块
    stmt->Koo((BaseAST*)(this));
    while_entry.pop();
    while_end.pop();
    return "";
  }
};

class OtherToBreakAST : public BaseAST{
  void Dump() const override {
    cout << "break";
  }

  string Koo(BaseAST* next) const override{
    auto peace = bbs_end.find(bbs_now);
    if(peace == bbs_end.end())
      cout << "break_bbs_now:" << bbs_now << "has no end" << endl;

    koopa_str = koopa_str + "jump %_" + to_string(while_end.top()) + "\n";
    bbs_end[bbs_now] = -1;
    return "";
  }
};

class OtherToContinueAST : public BaseAST{
  void Dump() const override {
    cout << "continue";
  }

  string Koo(BaseAST* next) const override{auto peace = bbs_end.find(bbs_now);
    if(peace == bbs_end.end())
      cout << "continue_bbs_now:" << bbs_now << "has no end" << endl;

    koopa_str = koopa_str + "jump %_" + to_string(while_entry.top()) + "\n";
    bbs_end[bbs_now] = -1;
    return "";
  }
};

class OtherToUselessAST : public BaseAST{
  public:
  BaseAST* exp = NULL;

  void Dump() const override {
    if(Valid(exp)){
      cout << "OtherToUselessAST { ";
      exp->Dump();
      cout << " }";
    }
    else
      cout << "OtherToUselessAST";
  }

  string Koo(BaseAST* next) const override {
    if(Valid(exp))
      exp->Koo(next);
    return "";
  }
};

class OtherToBlockAST : public BaseAST{
  public:
  unique_ptr<BaseAST> block;

  void Dump() const override {
    cout << "OtherToBlockAST { ";
    block->Dump();
    cout << " }";
  }

  string Koo(BaseAST* next) const override {
    block->Koo(next);
    return "";
  }
};

class OtherToAssignAST : public BaseAST{
  public:
  unique_ptr<BaseAST> lVal;
  unique_ptr<BaseAST> exp;

  void Dump() const override {
    cout << "OtherToAssignAST { ";
    lVal->Dump();
    cout << ", ";
    exp->Dump();
    cout << " }";
  }

  string Koo(BaseAST* next) const override {
    string ident = lVal->Koo();
    string from = exp->Koo(next);
    if(ident[0] == '%') // 数组 因为store只能移到@不能%
      koopa_str = koopa_str + "store " + from + ", " + ident +"\n";
    else
      Store(ident + to_string(top->find(ident).val), from);
    return "";
  }
};

// 要改bbs_end表
class OtherToReturnAST : public BaseAST{
  public:
  BaseAST* exp = NULL;

  void Dump() const override {
    if(Valid(exp)){
      cout << "OtherToReturnAST { ";
      exp->Dump();
      cout << " }";
    }
    else
      cout << "OtherToReturnAST";
  }

  string Koo(BaseAST* next) const override{
    if(Valid(exp)){
      string ret_val = exp->Koo(next);
      koopa_str = koopa_str + "ret " + ret_val + "\n";
    }
    else
      koopa_str += "ret\n";

    // 当前基本块已经结束了 更改bbs_end为-1
    if(bbs_end.find(bbs_now) == bbs_end.end())
      cout << "return_bbs_now:" << bbs_now << "has no end" << endl;
    else
      bbs_end[bbs_now] = -1;
    return "";
  }
};

class ExpAST : public BaseAST{
  public:
  unique_ptr<BaseAST> lOrExp;

  void Dump() const override {
    cout << "ExpAST { ";
    lOrExp->Dump();
    cout << " }";
  }

  int Compute() override {
    return lOrExp->Compute();
  }

  string Koo(BaseAST* next) const override{
    return lOrExp->Koo(next);
  }
};

class LOrExpToLAndAST : public BaseAST{
  public:
  unique_ptr<BaseAST> lAndExp;

  void Dump() const override {
    cout << "LOrExpToLAndAST { ";
    lAndExp->Dump();
    cout << " }";
  }

  int Compute() override {
    return lAndExp->Compute();
  }

  string Koo(BaseAST* next) const override{
    return lAndExp->Koo(next);
  }
};

// short circuit
class LOrExpToTwoAST : public BaseAST{
  public:
  unique_ptr<BaseAST> lOrExp;
  // string lOrOp;
  unique_ptr<BaseAST> lAndExp;

  void Dump() const override {
    cout << "LOrExpToTwoAST { ";
    lOrExp->Dump();
    cout << ", ||";
    lAndExp->Dump();
    cout << " }";
  }

  int Compute() override {
    return Compute_Op(lOrExp->Compute(), lAndExp->Compute(), "||");
  }
  
  string Koo(BaseAST* next) const override{
    string short_circuit = lOrExp->Koo();

    string tmp_reg = "result" + to_string(bbs_now);
    Alloc(tmp_reg);
    Store(tmp_reg, "1");
    int then_bbs = bbs_num, else_bbs = bbs_num + 1, back_bbs = bbs_num + 2;
    bbs_num += 3;
    Bind(then_bbs, back_bbs);
    Bind(else_bbs, back_bbs);
    Inherit(bbs_now, back_bbs);

    // short_circuit非零->then_bbs: jump back_bbs; short_circuit为零->else_bbs: 继续算
    Branch(short_circuit, then_bbs, else_bbs, back_bbs);
    bbs_now = then_bbs;
    
    Jump(bbs_now);
    bbs_now = else_bbs;
    print_bbs_begin(else_bbs);
    koo_ret zero, lAnd = koo_parse(lAndExp->Koo());
    Store(tmp_reg, koo_binary(zero, lAnd, "!="));

    Jump(bbs_now);
    bbs_now = back_bbs;
    print_bbs_begin(back_bbs);
    return Load(tmp_reg);
  }
};

class LAndExpToEqAST : public BaseAST{
  public:
  unique_ptr<BaseAST> eqExp;

  void Dump() const override {
    cout << "LAndExpToEqAST { ";
    eqExp->Dump();
    cout << " }";
  }

  int Compute() override {
    return eqExp->Compute();
  }
  
  string Koo(BaseAST* next) const override{
    return eqExp->Koo(next);
  }
};

// short circuit
class LAndExpToTwoAST : public BaseAST{
  public:
  unique_ptr<BaseAST> lAndExp;
  // string lAndOp;
  unique_ptr<BaseAST> eqExp;

  void Dump() const override {
    cout << "LAndExpToTwoAST { ";
    lAndExp->Dump();
    cout << ", ";
    eqExp->Dump();
    cout << " }";
  }

  int Compute() override {
    return Compute_Op(lAndExp->Compute(), eqExp->Compute(), "&&");
  }
  
  string Koo(BaseAST* next) const override{
    string short_circuit = lAndExp->Koo();

    string tmp_reg = "result" + to_string(bbs_now);
    Alloc(tmp_reg);
    Store(tmp_reg, "0");
    int then_bbs = bbs_num, else_bbs = bbs_num + 1, back_bbs = bbs_num + 2;
    bbs_num += 3;
    Bind(then_bbs, back_bbs);
    Bind(else_bbs, back_bbs);
    Inherit(bbs_now, back_bbs);
    
    // short_circuit非零->then_bbs: 继续算; short_circuit为零->else_bbs: jump back_bbs
    Branch(short_circuit, then_bbs, else_bbs, back_bbs);
    bbs_now = then_bbs;
    koo_ret zero, eq = koo_parse(eqExp->Koo());
    Store(tmp_reg, koo_binary(zero, eq, "!="));

    Jump(bbs_now);
    bbs_now = else_bbs;
    print_bbs_begin(else_bbs);
    
    Jump(bbs_now);
    bbs_now = back_bbs;
    print_bbs_begin(back_bbs);
    return Load(tmp_reg);
  }
};

class EqExpToRelAST : public BaseAST{
  public:
  unique_ptr<BaseAST> relExp;

  void Dump() const override {
    cout << "EqExpToRelAST { ";
    relExp->Dump();
    cout << " }";
  }

  int Compute() override {
    return relExp->Compute();
  }
  
  string Koo(BaseAST* next) const override{
    return relExp->Koo(next);
  }
};

class EqExpToTwoAST : public BaseAST{
  public:
  unique_ptr<BaseAST> eqExp;
  string eqOp;
  unique_ptr<BaseAST> relExp;

  void Dump() const override {
    cout << "EqExpToTwoAST { ";
    eqExp->Dump();
    cout << ", " << eqOp << ", ";
    relExp->Dump();
    cout << " }";
  }

  int Compute() override {
    return Compute_Op(eqExp->Compute(), relExp->Compute(), eqOp);
  }
  
  string Koo(BaseAST* next) const override{
    koo_ret eq = koo_parse(eqExp->Koo(next)), rel = koo_parse(relExp->Koo(next));
    return koo_binary(eq, rel, eqOp);
  }
};

class RelExpToAddAST : public BaseAST{
  public:
  unique_ptr<BaseAST> addExp;

  void Dump() const override {
    cout << "RelExpToAddAST { ";
    addExp->Dump();
    cout << " }";
  }

  int Compute() override {
    return addExp->Compute();
  }
  
  string Koo(BaseAST* next) const override{
    return addExp->Koo(next);
  }
};

class RelExpToTwoAST : public BaseAST{
  public:
  unique_ptr<BaseAST> relExp;
  string relOp;
  unique_ptr<BaseAST> addExp;

  void Dump() const override {
    cout << "RelExpToTwoAST { ";
    relExp->Dump();
    cout << ", " << relOp << ", ";
    addExp->Dump();
    cout << " }";
  }

  int Compute() override {
    return Compute_Op(relExp->Compute(), addExp->Compute(), relOp);
  }
  
  string Koo(BaseAST* next) const override{
    koo_ret rel = koo_parse(relExp->Koo(next)), add = koo_parse(addExp->Koo(next));
    return koo_binary(rel, add, relOp);
  }
};

class AddExpToMulAST : public BaseAST{
  public:
  unique_ptr<BaseAST> mulExp;

  void Dump() const override {
    cout << "AddExpToMulAST { ";
    mulExp->Dump();
    cout << " }";
  }

  int Compute() override {
    return mulExp->Compute();
  }

  string Koo(BaseAST* next) const override{
    return mulExp->Koo(next);
  }
};

class AddExpToTwoAST : public BaseAST{
  public:
  unique_ptr<BaseAST> addExp;
  string addOp;
  unique_ptr<BaseAST> mulExp;

  void Dump() const override {
    cout << "AddExpToTwoAST { ";
    addExp->Dump();
    cout << ", " << addOp << ", ";
    mulExp->Dump();
    cout << " }";
  }

  int Compute() override {
    return Compute_Op(addExp->Compute(), mulExp->Compute(), addOp);
  }

  string Koo(BaseAST* next) const override{
    koo_ret add = koo_parse(addExp->Koo(next)), mul = koo_parse(mulExp->Koo(next));
    return koo_binary(add, mul, addOp);
  }
};

class MulExpToUnaryAST : public BaseAST{
  public:
  unique_ptr<BaseAST> unaryExp;

  void Dump() const override {
    cout << "MulExpToUnaryAST { ";
    unaryExp->Dump();
    cout << " }";
  }

  int Compute() override {
    return unaryExp->Compute();
  }

  string Koo(BaseAST* next) const override{
    return unaryExp->Koo(next);
  }
};

class MulExpToTwoAST : public BaseAST{
  public:
  unique_ptr<BaseAST> mulExp;
  string mulOp;
  unique_ptr<BaseAST> unaryExp;

  void Dump() const override {
    cout << "MulExpToTwoAST { ";
    mulExp->Dump();
    cout << ", " << mulOp << ", ";
    unaryExp->Dump();
    cout << " }";
  }

  int Compute() override {
    return Compute_Op(mulExp->Compute(), unaryExp->Compute(), mulOp);
  }

  string Koo(BaseAST* next) const override{
    koo_ret mul = koo_parse(mulExp->Koo(next)), una = koo_parse(unaryExp->Koo(next));
    return koo_binary(mul, una, mulOp);
  }
};

class UnaryExpToOneAST : public BaseAST{
  public:
  unique_ptr<BaseAST> primaryExp;

  void Dump() const override {
    cout << "UnaryExpToOneAST { ";
    primaryExp->Dump();
    cout << " }";
  }

  int Compute() override {
    return primaryExp->Compute();
  }

  string Koo(BaseAST* next) const override{
    return primaryExp->Koo(next);
  }
};

class UnaryExpToTwoAST : public BaseAST{
  public:
  string unaryOp;
  unique_ptr<BaseAST> unaryExp;

  void Dump() const override {
    cout << "UnaryExpToTwoAST { " << unaryOp << ", ";
    unaryExp->Dump();
    cout << " }";
  }

  int Compute() override {
    int tmp = unaryExp->Compute();
    switch(unaryOp[0]){
      case '+': return tmp;
      case '-': return -tmp;
      case '!': return !tmp;
      default: break;
    }
    return 0;
  }

  string Koo(BaseAST* next) const override{
    if(unaryOp[0] == '+')
      return unaryExp->Koo(next);
    koo_ret zero;
    koo_ret una = koo_parse(unaryExp->Koo(next));
    return koo_binary(zero, una, unaryOp);
  }
};

class UnaryExpToFuncAST : public BaseAST{
  public:
  string ident;
  BaseAST* funcRParams;

  void Dump() const override {
    cout << "UnaryExpToFuncAST { " << ident << ", ";
    if(Valid(funcRParams)){
      funcRParams->Dump();
      cout << ", ";
    }
    cout << " }";
  }

  string Koo(BaseAST* next) const override{
    string para_str = "";
    if(Valid(funcRParams))
      para_str = funcRParams->Koo();
    
    string cat = "";
    if(ret_func[ident]){
      cat = "%" + to_string(koopa_reg++);
      koopa_str = koopa_str + cat + " = ";
    }
    koopa_str = koopa_str + "call @" + ident + "(" + para_str + ")\n";
    return cat;
  }
};

class FuncRParamsAST : public BaseAST{
  public:
  vector<BaseAST*> exp_;

  void Dump() const override {
    cout << "FuncRParamsAST { ";
    for(int i = 0; i < exp_.size(); i++)
      exp_[i]->Dump();
    cout << " }";
  }

  string Koo(BaseAST* next) const override{
    int len = exp_.size();
    vector<string> j;
    // 这个Koo返回的是call @f()中的所有参数，和其他Koo不同
    string ret_str;

    whether_load = false;
    for(int i = 0; i < len; i++){
      j.push_back(exp_[i]->Koo());
    }
    whether_load = true;
    for(int i = 0; i < len; i++){
      ret_str += j[i];
      if(i != len-1){
        ret_str += ", ";
      }
    }
    return ret_str;
  }
};

class PrimaryExpToExpAST : public BaseAST{
  public:
  unique_ptr<BaseAST> exp;
  
  void Dump() const override {
    cout << "PrimaryExpToExpAST { ";
    exp->Dump();
    cout << " }";
  }

  int Compute() override {
    return exp->Compute();
  }

  string Koo(BaseAST* next) const override{
    return exp->Koo(next);
  }
};

class PrimaryExpToLValAST : public BaseAST{
  public:
  unique_ptr<BaseAST> lVal;

  void Dump() const override {
    cout << "PrimaryExpToLValAST { ";
    lVal->Dump();
    cout << " }";
  }

  int Compute() override {
    return lVal->Compute();
  }

  string Koo(BaseAST* next) const override {
    string ident = lVal->Koo();
    if(ident[0] == '%'){ // 数组 因为load只能从@移不能从%移
      if(whether_load){
        koopa_str = koopa_str + "%" + to_string(koopa_reg++) + " = load " + ident + "\n";
        return "%" + to_string(koopa_reg-1);
      }
      return ident;
    }
    else{
      Inf lVal = top->find(ident);
      if(lVal.var){ // 是var
        return Load(ident + to_string(lVal.val));
      }
      else // 是const
        return to_string(lVal.val);
    }
  }
};

class LValAST : public BaseAST{
  public:
  string ident;
  vector<BaseAST*> exp_;

  void Dump() const override {
    cout << "LValAST {" << ident;
    for(int i = 0; i < exp_.size(); i++)
      exp_[i]->Dump();
    cout << " }";
  }

  int Compute() override{
    // 一定是非数组的const
    return top->find(ident).val;
  }

  string Koo(BaseAST* next) const override{
    // 非数组返回ident 数组getptr/getelemptr以后返回%x
    int categ = top->find_category(ident);
    switch(categ){
      case c_num: 
        return ident;

      case c_array:{
        string name = ident + to_string(top->find_array(ident)->val);
        int len = exp_.size();
        vector<string> offset;
        for(int i = 0; i < len; i++)
          offset.push_back(exp_[i]->Koo());
        string re = Getelemptr_only("@" + name, offset);

        // 用来传参
        if(!whether_load){
          int para_len = top->find_array(ident)->len;
          if(para_len == len) // 传参是int 要load出来
            koopa_str = koopa_str + "%" + to_string(koopa_reg++) + " = load " + re + "\n";
          else // 传参是地址
            koopa_str = koopa_str + "%" + to_string(koopa_reg++) + " = getelemptr " + re + ", 0\n";
          return "%" + to_string(koopa_reg-1);
        }
        return re;
      }

      case c_func_array:{
        string name = ident + to_string(top->find_func_array(ident)->val);
        int len = exp_.size();

        vector<string> offset;
        for(int i = 0; i < len; i++)
          offset.push_back(exp_[i]->Koo());
        koopa_str = koopa_str + "%" + to_string(koopa_reg) + " = load @" + name + "\n";
        string re = "%" + to_string(koopa_reg++);
        if(len != 0){
          koopa_str = koopa_str + "%" + to_string(koopa_reg) + " = getptr %" + to_string(koopa_reg-1) + ", " + offset[0] + "\n";
          name = "%" + to_string(koopa_reg++);
          vector<string> cut_offset;
          cut_offset.assign(offset.begin()+1, offset.end());
          re = Getelemptr_only(name, cut_offset);
        }

        // 用来传参
        if(!whether_load){
          int para_len = top->find_func_array(ident)->len;
          //cout << "func_array: (para_len, len)" << para_len << ", " << len << endl;
          if(para_len == len - 1) // 传参是int 要load出来
            koopa_str = koopa_str + "%" + to_string(koopa_reg++) + " = load " + re + "\n";
          else if(len == 0) // 其实不操作也行
            koopa_str = koopa_str + "%" + to_string(koopa_reg++) + " = getptr " + re + ", 0\n";
          else
            koopa_str = koopa_str + "%" + to_string(koopa_reg++) + " = getelemptr " + re + ", 0\n";
          return "%" + to_string(koopa_reg-1);
        }
        return re;
      }

      default:
        break;
    }
    return "";
  }
};

class PrimaryExpToNumAST : public BaseAST{
  public:
  int number;

  void Dump() const override {
    cout << "PrimaryExpToNumAST { " << to_string(number) << " }";
  }

  int Compute() override {
    return number;
  }

  string Koo(BaseAST* next) const override{
    return to_string(number);
  }
};
