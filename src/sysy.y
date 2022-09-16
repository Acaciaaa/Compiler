%code requires {
  #include <memory>
  #include <string>
}

%{

#include <iostream>
#include <memory>
#include <string>
#include "myast.h"

// 声明 lexer 函数和错误处理函数
int yylex();
void yyerror(std::unique_ptr<BaseAST> &ast, const char *s);

using namespace std;

%}

// 定义 parser 函数和错误处理函数的附加参数
// 我们需要返回一个字符串作为 AST, 所以我们把附加参数定义成字符串的智能指针
// 解析完成后, 我们要手动修改这个参数, 把它设置成解析得到的字符串
%parse-param { std::unique_ptr<BaseAST> &ast }

// yylval 的定义, 我们把它定义成了一个联合体 (union)
// 因为 token 的值有的是字符串指针, 有的是整数
// 之前我们在 lexer 中用到的 str_val 和 int_val 就是在这里被定义的
// 至于为什么要用字符串指针而不直接用 string 或者 unique_ptr<string>?
// 请自行 STFW 在 union 里写一个带析构函数的类会出现什么情况
%union {
  std::string *str_val;
  int int_val;
  BaseAST *ast_val;
  vector<BaseAST*> *vec_val;
}

// lexer 返回的所有 token 种类的声明
// 注意 IDENT 和 INT_CONST 会返回 token 的值, 分别对应 str_val 和 int_val
%token INT VOID RETURN LE GE EQ NE LAND LOR CONST IF ELSE WHILE BREAK CONTINUE
%token <str_val> IDENT
%token <int_val> INT_CONST

// 非终结符的类型定义
%type <ast_val> CompUnit FuncDef Block Stmt Exp LOrExp LAndExp EqExp RelExp AddExp MulExp UnaryExp PrimaryExp
%type <int_val> Number
%type <str_val> UnaryOp MulOp EqOp RelOp

%type <ast_val> BlockItem Decl ConstDecl VarDecl BType ConstDef VarDef ConstInitVal InitVal ConstExp
%type <vec_val> BlockItem_ ConstDef_ VarDef_ 

%type <ast_val> Exp_ Matched Unmatched Other 

%type <vec_val> FuncFParam_ Exps
%type <ast_val> CompUnit_ FuncFParams_ FuncFParams FuncFParam FuncRParams_ FuncRParams

%type <vec_val> Exp_vector ConstExp_vector ConstInitVal_vector InitVal_vector
%type <ast_val> LVal
%%

// 开始符, CompUnit ::= FuncDef, 大括号后声明了解析完成后 parser 要做的事情
// 之前我们定义了 FuncDef 会返回一个 str_val, 也就是字符串指针
// 而 parser 一旦解析完 CompUnit, 就说明所有的 token 都被解析了, 即解析结束了
// 此时我们应该把 FuncDef 返回的结果收集起来, 作为 AST 传给调用 parser 的函数
// $1 指代规则里第一个符号的返回值, 也就是 FuncDef 的返回值
S
  : CompUnit {
    auto sast = make_unique<SAST>();
    sast->compUnit = unique_ptr<BaseAST>($1);
    ast = move(sast);
  }
  ;

CompUnit
  : CompUnit_ FuncDef {
    auto ast = new CompUnitAST();
    ast->compUnit = $1;
    ast->funcDef = unique_ptr<BaseAST>($2);
    ast->decl = NULL;
    ast->func = true;
    $$ = ast;
  }
  | CompUnit_ Decl {
    auto ast = new CompUnitAST();
    ast->compUnit = $1;
    ast->funcDef = NULL;
    ast->decl = unique_ptr<BaseAST>($2);
    ast->func = false;
    $$ = ast;
  }
  ;

CompUnit_
  : {
    $$ = NULL;
  }
  | CompUnit {
    $$ = $1;
  }
  ;

// FuncDef ::= FuncType IDENT '(' ')' Block;
// 我们这里可以直接写 '(' 和 ')', 因为之前在 lexer 里已经处理了单个字符的情况
// 解析完成后, 把这些符号的结果收集起来, 然后拼成一个新的字符串, 作为结果返回
// $$ 表示非终结符的返回值, 我们可以通过给这个符号赋值的方法来返回结果
// 你可能会问, FuncType, IDENT 之类的结果已经是字符串指针了
// 为什么还要用 unique_ptr 接住它们, 然后再解引用, 把它们拼成另一个字符串指针呢
// 因为所有的字符串指针都是我们 new 出来的, new 出来的内存一定要 delete
// 否则会发生内存泄漏, 而 unique_ptr 这种智能指针可以自动帮我们 delete
// 虽然此处你看不出用 unique_ptr 和手动 delete 的区别, 但当我们定义了 AST 之后
// 这种写法会省下很多内存管理的负担
FuncDef
  : BType IDENT '(' FuncFParams_ ')' Block {
    auto ast = new FuncDefAST();
    ast->funcType = unique_ptr<BaseAST>($1);
    ast->ident = *unique_ptr<string>($2);
    ast->funcFParams = $4;
    ast->block = unique_ptr<BaseAST>($6);
    $$ = ast;
  }
  ;

// 同上, 不再解释
BType
  : INT {
    auto ast = new BTypeAST();
    ast->type = "int";
    $$ = ast;
  }
  | VOID {
    auto ast = new BTypeAST();
    ast->type = "void";
    $$ = ast;
  }
  ;

// 零次、一次
FuncFParams_
  : {
    $$ = NULL;
  }
  | FuncFParams {
    $$ = $1;
  }
  ;

// 真实AST
FuncFParams
  : FuncFParam_ FuncFParam {
    auto ast = new FuncFParamsAST();
    if($1){
    ast->funcFParam_.assign(($1)->begin(), ($1)->end());
    delete($1);}
    ast->funcFParam_.push_back($2);
    $$ = ast;
  }
  ;

// FuncFParam的vector
FuncFParam_
  : FuncFParam_ FuncFParam ',' {
    auto v = new vector<BaseAST*>;
    if($1){
    v->assign(($1)->begin(), ($1)->end());
    delete($1);}
    v->push_back($2);
    $$ = v;
  }
  | {
    $$ = NULL;
  }
  ;

// 真实AST
FuncFParam
  : BType IDENT {
    auto ast = new FuncFParamIntAST();
    ast->bType = unique_ptr<BaseAST>($1);
    ast->ident = *unique_ptr<std::string>($2);
    $$ = ast;
  }
  | BType IDENT '[' ']' ConstExp_vector {
    auto ast = new FuncFParamArrayAST();
    ast->bType = unique_ptr<BaseAST>($1);
    ast->ident = *unique_ptr<std::string>($2);
    if($5){
      ast->constExp_.assign(($5)->begin(), ($5)->end());
      delete($5);
    }
    $$ = ast;
  }
  ;

Block
  : '{' BlockItem_ '}' {
    auto ast = new BlockAST();
    if(($2)){
    ast->blockItem_.assign(($2)->begin(), ($2)->end());
    delete ($2);}
    $$ = ast;
  }
  ;

BlockItem_
  : BlockItem_ BlockItem {
    auto v = new vector<BaseAST*>;
    if(($1)){
    v->assign(($1)->begin(), ($1)->end());
    delete ($1);}
    v->push_back($2);
    $$ = v;
  }
  | {
    $$ = NULL;
  }
  ;

BlockItem
  : Decl {
    auto ast = new BlockItemToDeclAST();
    ast->decl = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  | Stmt {
    auto ast = new BlockItemToStmtAST();
    ast->stmt = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  ;

Stmt
  : Matched {
    auto ast = new StmtToMatchedAST();
    ast->matched = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  | Unmatched {
    auto ast = new StmtToUnmatchedAST();
    ast->unmatched = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  ;

Matched
  : IF '(' Exp ')' Matched ELSE Matched {
    auto ast = new MatchedToIfAST();
    ast->exp = unique_ptr<BaseAST>($3);
    ast->matched1 = unique_ptr<BaseAST>($5);
    ast->matched2 = unique_ptr<BaseAST>($7);
    $$ = ast;
  }
  | Other {
    auto ast = new MatchedToOtherAST();
    ast->other = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  ;

Unmatched
  : IF '(' Exp ')' Stmt {
    auto ast = new UnmatchedToOneAST();
    ast->exp = unique_ptr<BaseAST>($3);
    ast->stmt = unique_ptr<BaseAST>($5);
    $$ = ast;
  }
  | IF '(' Exp ')' Matched ELSE Unmatched {
    auto ast = new UnmatchedToTwoAST();
    ast->exp = unique_ptr<BaseAST>($3);
    ast->matched = unique_ptr<BaseAST>($5);
    ast->unmatched = unique_ptr<BaseAST>($7);
    $$ = ast;
  }
  ;

Decl
  : ConstDecl {
    auto ast = new DeclToConstAST();
    ast->constDecl = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  | VarDecl {
    auto ast = new DeclToVarAST();
    ast->varDecl = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  ;

ConstDecl
  : CONST BType ConstDef_ ';' {
    auto ast = new ConstDeclAST();
    ast->bType = unique_ptr<BaseAST>($2);
    ast->constDef_.assign(($3)->begin(), ($3)->end());
    delete ($3);
    $$ = ast;
  }
  ;

VarDecl
  : BType VarDef_ ';' {
    auto ast = new VarDeclAST();
    ast->bType = unique_ptr<BaseAST>($1);
    ast->varDef_.assign(($2)->begin(), ($2)->end());
    delete ($2);
    $$ = ast;
  }
  ;

ConstDef_
  : ConstDef_ ',' ConstDef {
    auto v = new vector<BaseAST*>;
    v->assign(($1)->begin(), ($1)->end());
    delete ($1);
    v->push_back($3);
    $$ = v;
  }
  | ConstDef {
    auto v = new vector<BaseAST*>;
    v->push_back($1);
    $$ = v;
  }
  ;

VarDef_
  : VarDef_ ',' VarDef {
    auto v = new vector<BaseAST*>;
    v->assign(($1)->begin(), ($1)->end());
    delete ($1);
    v->push_back($3);
    $$ = v;
  }
  | VarDef {
    auto v = new vector<BaseAST*>;
    v->push_back($1);
    $$ = v;
  }
  ;

ConstDef
  : IDENT ConstExp_vector '=' ConstInitVal {
    auto ast = new ConstDefAST();
    ast->ident = *unique_ptr<std::string>($1);
    if($2){
    ast->constExp_.assign(($2)->begin(), ($2)->end());
    delete($2);}
    ast->constInitVal = unique_ptr<BaseAST>($4);
    $$ = ast;
  }
  ;

VarDef
  : IDENT ConstExp_vector '=' InitVal {
    auto ast = new VarDefToTwoAST();
    ast->ident = *unique_ptr<std::string>($1);
    if($2){
    ast->constExp_.assign(($2)->begin(), ($2)->end());
    delete($2);}
    ast->initVal = unique_ptr<BaseAST>($4);
    $$ = ast;
  }
  | IDENT ConstExp_vector {
    auto ast = new VarDefToOneAST();
    ast->ident = *unique_ptr<std::string>($1);
    if($2){
    ast->constExp_.assign(($2)->begin(), ($2)->end());
    delete($2);}
    $$ = ast;
  }
  ;

ConstExp_vector
  : ConstExp_vector '[' ConstExp ']' {
    auto v = new vector<BaseAST*>;
    if(($1)){
    v->assign(($1)->begin(), ($1)->end());
    delete ($1);}
    v->push_back($3);
    $$ = v;
  }
  | {
    $$ = NULL;
  }
  ;

InitVal
  : Exp {
    auto ast = new InitValToOneAST();
    ast->exp = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  | '{' '}' {
    $$ = new InitValToArrayAST();
  }
  | '{' InitVal_vector InitVal '}' {
    auto ast = new InitValToArrayAST();
    if($2){
    ast->initVal_.assign(($2)->begin(), ($2)->end());
    delete($2);}
    ast->initVal_.push_back($3);
    $$ = ast;
  }
  ;

ConstInitVal
  : ConstExp {
    auto ast = new ConstInitValToOneAST();
    ast->constExp = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  | '{' '}' {
    $$ = new ConstInitValToArrayAST();
  }
  | '{' ConstInitVal_vector ConstInitVal '}' {
    auto ast = new ConstInitValToArrayAST();
    if($2){
    ast->constInitVal_.assign(($2)->begin(), ($2)->end());
    delete($2);}
    ast->constInitVal_.push_back($3);
    $$ = ast;
  }
  ;

ConstInitVal_vector
  : ConstInitVal_vector ConstInitVal ',' {
    auto v = new vector<BaseAST*>;
    if(($1)){
    v->assign(($1)->begin(), ($1)->end());
    delete ($1);}
    v->push_back($2);
    $$ = v;
  }
  | {
    $$ = NULL;
  }
  ;

  
InitVal_vector
  : InitVal_vector InitVal ',' {
    auto v = new vector<BaseAST*>;
    if(($1)){
    v->assign(($1)->begin(), ($1)->end());
    delete ($1);}
    v->push_back($2);
    $$ = v;
  }
  | {
    $$ = NULL;
  }
  ;

ConstExp
  : Exp {
    auto ast = new ConstExpAST();
    ast->exp = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  ;

Other
  : RETURN Exp_ ';' {
    auto ast = new OtherToReturnAST();
    ast->exp = $2;
    $$ = ast;
  }
  | LVal '=' Exp ';' {
    auto ast = new OtherToAssignAST();
    ast->lVal = unique_ptr<BaseAST>($1);
    ast->exp = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  | Block {
    auto ast = new OtherToBlockAST();
    ast->block = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  | Exp_ ';' {
    auto ast = new OtherToUselessAST();
    ast->exp = $1;
    $$ = ast;
  }
  | WHILE '(' Exp ')' Stmt {
    auto ast = new OtherToWhileAST();
    ast->exp = unique_ptr<BaseAST>($3);
    ast->stmt = unique_ptr<BaseAST>($5);
    $$ = ast;
  }
  | BREAK ';' {
    $$ = new OtherToBreakAST();
  }
  | CONTINUE ';' {
    $$ = new OtherToContinueAST();
  }
  ;

Exp_
  : {
    $$ = NULL;
  }
  | Exp {
    $$ = $1;
  }
  ;

Exp
  : LOrExp {
    auto ast = new ExpAST();
    ast->lOrExp = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  ;

LOrExp
  : LAndExp {
    auto ast = new LOrExpToLAndAST();
    ast->lAndExp = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  | LOrExp LOR LAndExp {
    auto ast = new LOrExpToTwoAST();
    ast->lOrExp = unique_ptr<BaseAST>($1);
    ast->lAndExp = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  ;

LAndExp
  : EqExp {
    auto ast = new LAndExpToEqAST();
    ast->eqExp = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  | LAndExp LAND EqExp {
    auto ast = new LAndExpToTwoAST();
    ast->lAndExp = unique_ptr<BaseAST>($1);
    ast->eqExp = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  ;

EqExp
  : RelExp {
    auto ast = new EqExpToRelAST();
    ast->relExp = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  | EqExp EqOp RelExp {
    auto ast = new EqExpToTwoAST();
    ast->eqExp = unique_ptr<BaseAST>($1);
    ast->eqOp = *unique_ptr<std::string>($2);
    ast->relExp = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  ;

RelExp
  : AddExp {
    auto ast = new RelExpToAddAST();
    ast->addExp = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  | RelExp RelOp AddExp {
    auto ast = new RelExpToTwoAST();
    ast->relExp = unique_ptr<BaseAST>($1);
    ast->relOp = *unique_ptr<std::string>($2);
    ast->addExp = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  ;

AddExp
  : MulExp {
    auto ast = new AddExpToMulAST();
    ast->mulExp = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  | AddExp UnaryOp MulExp {
    auto ast = new AddExpToTwoAST();
    ast->addExp = unique_ptr<BaseAST>($1);
    ast->addOp = *unique_ptr<std::string>($2);
    ast->mulExp = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  ;

MulExp
  : UnaryExp {
    auto ast = new MulExpToUnaryAST();
    ast->unaryExp = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  | MulExp MulOp UnaryExp {
    auto ast = new MulExpToTwoAST();
    ast->mulExp = unique_ptr<BaseAST>($1);
    ast->mulOp = *unique_ptr<std::string>($2);
    ast->unaryExp = unique_ptr<BaseAST>($3);
    $$ = ast;
  }
  ;

PrimaryExp
  : '(' Exp ')' {
    auto ast = new PrimaryExpToExpAST();
    ast->exp = unique_ptr<BaseAST>($2);
    $$ = ast;
  }
  | LVal {
    auto ast = new PrimaryExpToLValAST();
    ast->lVal = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  | Number{
    auto ast = new PrimaryExpToNumAST();
    ast->number = $1;
    $$ = ast;
  }
  ;

LVal
  : IDENT Exp_vector {
    auto ast = new LValAST();
    ast->ident = *unique_ptr<std::string>($1);
    if($2){
    ast->exp_.assign(($2)->begin(), ($2)->end());
    delete($2);}
    $$ = ast;
  }
  ;

Exp_vector
  : Exp_vector '[' Exp ']' {
    auto v = new vector<BaseAST*>;
    if(($1)){
    v->assign(($1)->begin(), ($1)->end());
    delete ($1);}
    v->push_back($3);
    $$ = v;
  }
  | {
    $$ = NULL;
  }
  ;

UnaryExp
  : PrimaryExp {
    auto ast = new UnaryExpToOneAST();
    ast->primaryExp = unique_ptr<BaseAST>($1);
    $$ = ast;
  }
  | UnaryOp UnaryExp{
    auto ast = new UnaryExpToTwoAST();
    ast->unaryOp = *unique_ptr<std::string>($1);
    ast->unaryExp = unique_ptr<BaseAST>($2);
    $$ = ast;
  }
  | IDENT '(' FuncRParams_ ')' {
    auto ast = new UnaryExpToFuncAST();
    ast->ident = *unique_ptr<std::string>($1);
    ast->funcRParams = $3;
    $$ = ast;
  }
  ;

// 零次、一次
FuncRParams_
  : FuncRParams {
    $$ = $1;
  }
  | {
    $$ = NULL;
  }
  ;

// 真实AST
FuncRParams
  : Exps Exp {
    auto ast = new FuncRParamsAST();
    if($1){
    ast->exp_.assign(($1)->begin(), ($1)->end());
    delete($1);}
    ast->exp_.push_back($2);
    $$ = ast;
  }
  ;

// Exp的vector
Exps
  : Exps Exp ',' {
    auto v = new vector<BaseAST*>;
    if(($1)){
    v->assign(($1)->begin(), ($1)->end());
    delete ($1);}
    v->push_back($2);
    $$ = v;
  }
  | {
    $$ = NULL;
  }
  ;

RelOp
  : '<' {
    $$ = new string("<");
  }
  | '>' {
    $$ = new string(">");
  }
  | LE {
    $$ = new string("<=");
  }
  | GE {
    $$ = new string(">=");
  }
  ;

EqOp
  : EQ {
    $$ = new string("==");
  }
  | NE {
    $$ = new string("!=");
  }
  ;

MulOp
  : '*' {
    $$ = new string("*");
  }
  | '/' {
    $$ = new string("/");
  }
  | '%' {
    $$ = new string("%");
  }
  ;

UnaryOp
  : '+' {
    $$ = new string("+");
  }
  | '-' {
    $$ = new string("-");
  }
  | '!' {
    $$ = new string("!");
  }
  ;

Number
  : INT_CONST {
    auto ast = new int($1);
    $$ = *ast;
  }
  ;

%%

// 定义错误处理函数, 其中第二个参数是错误信息
// parser 如果发生错误 (例如输入的程序出现了语法错误), 就会调用这个函数
void yyerror(unique_ptr<BaseAST> &ast, const char *s) {
  cerr << "error: " << s << endl;
}
