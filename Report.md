## 编译器概述
#### 基本功能
完成文档中 _Lv0-Lv9_ 的部分，读入命令`compiler -koopa/-riscv/-perf 输入文件 -o 输出文件`，根据第二个 _flag_ 生成中间代码文件/目标代码文件/目标代码文件，输入文件为 _SysY_ 语言程序，可识别文法包括全局变量常量数组、函数定义与调用、局部变量常量数组、 _if else_ 和 _while(break continue)_ 控制流语句、表达式，然后进行词法分析、语法分析、生成抽象语法树、调用 _AST->Koo()_ 深度搜索生成中间代码、通过 _libkooopa_ 得到表示中间代码之间关系的数据结构、遍历指令生成目标代码
#### 主要特点
暂无主要特点，完全按照文档的排布方式完成，没有使用额外工具或自己构造中间语言。词法分析部分在`sysy.l`，文法规范部分在`sysy.y`，抽象语法树定义在`myast.h`，一些中间代码生成或者相关函数包装在`koopa_gen.cpp`，符号表相关数据结构和相关函数包装在`table.cpp`，目标代码生成部分在`riscv_gen.cpp`，处理命令行要求的部分在`main.cpp`
## 编译器设计
#### 主要模块组成
- __词法分析部分`sysy.l`__ 定义了 _token_ 和正则表达式
- __文法推导部分`sysy.y`__ 定义了文法规范， _yylval_ 有四种，分别是字符串类型、整型、抽象语法树基类 _BaseAST*_ 、抽象语法树数组 _vector<BaseAST*>*_ ，每一步文法推导返回的都是以上四种之一。主要功能就是按照文法建立代表相互关系的抽象语法树
- __抽象语法树部分`myast.h`__ 定义了所有 _AST_ 以及一些全局变量，主要通过调用虚函数方法 _对象->Koo()_ 深度搜索生成中间代码
- __中间代码相关部分`koopa_gen.cpp table.cpp`__ 定义了一些相关数据结构和函数，比如处理控制流时需要的变量和函数、进行数组分配时的初始化处理、_SysY_ 库函数定义、符号表查找合并的方法等等
- __目标代码相关部分`riscv_gen.cpp`__ 定义了如何处理 _libkoopa_ 后的数据结构，比如所有函数、基本块、指令的 _Visit_ 函数；也定义了目标代码生成的辅助函数（应该和中间代码一样另起文件的），比如计算栈空间、生成 _prologue_ 和 _epilogue_ 等
- __入口文件`main.cpp`__ 接收命令行要求，拿到 _yyparse_ 后的`unique_ptr<BaseAST> &ast`，调用 _ast->Koo()_ 生成中间代码，调用 _gen riscv()_ 生成目标代码
#### 主要数据结构
- 抽象语法树
	```C++
	class BaseAST {
		public:	
		virtual ~BaseAST() = default;
		virtual void Dump() const = 0; // 保留了文档Lv1的方法，可以用来检查文法推导部分有无出错
		virtual string Koo(BaseAST* next = NULL) const = 0; // 最主要的函数：生成中间代码，返回值代表结果或结果位置，参数BaseAST*只在处理while时使用
		virtual int Compute(){return 0;} // 计算常量
		virtual Nest ComputeArray(){return Nest{};} // 数组维度
	};
	```
- 作用域与符号表
	```C++
	class Area {
		public:
		Area* father = NULL; // 上一层作用域
		int tag = 0; // 此作用域标号，每个函数初始化，后不断累加
		unordered_map<string, Inf> table; // 单值符号表，Inf记录是常量变量、值为多少
		unordered_map<string, Ainf> array; // 普通数组符号表，Ainf记录数组初始化值、维度等 a[2][3][4]
		unordered_map<string, Pinf> func_array; // 参数数组符号表，Pinf记录数组维度等 a[][4]
		// 各种方法……
	};
	```
- 控制流
	```C++
		unordered_map<BaseAST*, int> bbs_begin; // <代码位置，基本块标号>
		unordered_map<int, int> bbs_end; // <基本块标号，下一个基本块标号> 只要是兜底了的就为-1
		int bbs_now = 0; // 当前所在基本块
		int bbs_num = 1; // 基本块标号：累加量
		stack<int> while_entry; // 当前基本块的while入口：专给continue用
		stack<int> while_end; // 当前基本块的while出口：专给break用
	```
- 目标代码生成部分没有整体数据结构，主要就是 _libkoopa_ 定义好的结构，自己实现的部分都是生成代码的函数
#### 主要算法
主要算法在后续各个阶段编码细节有论述，概括来说就是表达式的求值自底向上传递（即 _string Koo()_ 的返回值），作用域和符号表通过嵌套的 _Area_ 实现进入和回退，控制流实时更新各个基本块的开始结束信息，数组指针在 _PrimaryExpToLValAST_ 中分情况处理
## 编译器实现
#### 所涉工具介绍
_libkoopa_ 是 _TA_ 提供的将中间代码转换为表示相互关系的数据结构的工具，它的最上层是程序，然后分为函数、基本块、指令或值，而每个指令会链接到其他的指令或值上，并且可以通过`koopa_raw_type_t ty->tag`递归算出字节大小（用在数组分配栈空间时），通过确定指令类型来强制类型转换调用自身的 _Visit_ 函数。总的来说是构造非常优美的工程代码，我几辈子也写不出来的那种
#### 各个阶段编码细节
1. main函数
	- **关键功能**
		
		可以解析带有注释的、只含 _return i32_ 模式的main函数
	
	- **具体实现方法**
		
		首先按照文档在`sysy.l`中定义好token和正则表达式，块注释部分处理如下：
		
		`BlockComent  \/\*([^"\*]*|\**"[^"]*"|\*+[^"\/\*])*\**\*\/`
		
		然后同样在`sysy.y`文件中定义好文法（与文档一模一样不再赘述）。在`myast.h`中定义抽象语法树的数据结构：_AST_ 基类为 _class BaseAST_ ，生成中间代码的虚函数为 _Koo()_ ，暂时无参数无返回值，输出完自己负责的部分后通过递归调用 _对象->Koo()_ 进行深度优先搜索。生成目标代码的文件为`riscv_gen.cpp`，此阶段只会碰到两种指令类型: 
		- `KOOPA_RVT_INTEGER` 全局定义 _const string reg[18]_ 的寄存器数组，排列顺序为 _x0 t0...t6 a0...a7 sp ra_ 。若为零则返回零，若非零则生成 _li reg, imm_ 的指令并返回此寄存器的在 _reg[18]_ 中的下标
		- `KOOPA_RVT_RETURN` 取到下标 _i_ 后生成 _mv a0, reg[i]_ 和 _ret_ 的指令

	- **编码难点**
		- 理解`sysy.l`和`sysy.y`的语法格式，理解整个编译器内文件的运作程序以便正确添加 _#include_ 
		- 理解 _libkoopa_ 的输出结果，每个指令内部的内容是通过指针指向此指令的，所以在函数`Visit(const koopa_raw_value_t &value)`中 __我使用了哈希表存储每个指令和其对应的结果（所有会产生结果的指令类型都进行此操作，后不再赘述）__ ，以便其他指令指回来的时候可以直接返回结果

2. 表达式
	- **关键功能**
		
		可以解析带有注释的、只含 _return express_ 模式的main函数
	
	- **具体实现方法**
		
		首先直接在`sysy.l`中加入了除单个字符以外的识别模式，即 _<= >= == != && ||_ 。然后在`myast.h`中定义新增的数据结构，对于 _A := B | C_ 的文法分别构建 _AtoBAST_ 和 _AtoCAST_ 两个类；给 _Koo()_ 函数加上返回值 _string_ ：通过识别是否有字符"%"来判断拿到的是临时变量还是立即数；将代码生成有关的步骤包装成方便调用的函数放在`koopa_gen.cpp`中。最后`riscv_gen.cpp`会碰到新的指令类型: 
		- `KOOPA_RVT_BINARY` 由于规定了立即数类型指令返回 _reg[18]_ 的下标，所以拿到的都是寄存器，直接生成对应运算的机器代码即可（此时还不考虑寄存器超出使用范围的问题）

	- **编码难点**
		用有限的机器指令拼凑某些运算:

		op | ops
		:------------ | :------------
		equal | xor+seqz 
		not equal | xor+snez 
		less equal | greater than+seqz 
		greater equal | less than+seqz 
		 
3. 常量变量(包括全局)
	- **关键功能**
		
		可以解析不同作用域内的常量变量定义的main函数
	
	- **具体实现方法**
		- 如何在`sysy.y`中处理{}和[]语法  
		在 _%union_ 结构中增加了`vector<BaseAST*> *vec_val`的类型，按如下拆解文法的方式实现：以 _Block := {BlockItem}_ 为例
		```bison
			%type <ast_val> Block BlockItem
			%type <vec_val> BlockItem_
			Block
			: '{' BlockItem_ '}' {
			}
			;
			BlockItem_
			: BlockItem_ BlockItem {
			}
			| { // 推出空
			}
			;
		```
		
		- 作用域和符号表的组织  
		  作用域用 _Area_ 类来表示，主要包括父节点 _Area *father_ 、符号表 _table_ 、标号 _tag_ 和一系列插入、查找、合并等方法：其中父节点是用来链接上一层作用域的，方便查找变量和执行完后回到上一层；符号表的数据结构是哈希表，关键码为符号名称（字符串类型），映射到一个自定义结构（记录它是变量还是常量，若是常量是多少）；_tag_ 在全局作用域下为0，每个函数开始初始化为1  
		  使用过程：每到一个作用域就换到新的作用域上，链接好父节点并且 _tag_ ++，保证一个函数内的所有作用域标号都不一样，执行完后回退到原来作用域并删除此作用域  
		  __当遇到常量定义时__ ，基类 _BaseAST_ 中添加专门用来计算的虚函数 _int Compute()_ ，通过递归调用 _对象->Compute()_ 来计算结果，然后插入当前符号表中；__当遇到变量定义时__ ，也要插入符号表但是情况更复杂一些，全局变量要生成 _global alloc_ 而其他的只需要 _alloc_ ，有初始化语句的如果不是常值表达式还得生成 _store_ 操作；__当表达式中遇到变量常量时__ ，一层层往父亲作用域查找符号表，如果是常量就直接返回数字，如果是变量就先进行 _load_ 后返回临时变量
		  
		 - 机器代码生成部分 以下为新增指令类型：
			- `KOOPA_RVT_ALLOC` 将变量和在栈中的 _offset_ 存到代表局部变量的哈希表`local_space`中(每个函数清除一次)，返回在栈中的 _offset_ 
			- `KOOPA_RVT_GLOBAL_ALLOC` 输出给变量分配堆空间的目标代码（注意这里的初始值如果是立即数不能够生成 _li_ 指令），然后将此变量存到全局数组`global_var`中并返回
			- `KOOPA_RVT_LOAD` 如果是全局变量先 _la_ 后 _lw_ ，普通的就直接生成`lw reg, imm(sp)`，然后存进栈后返回 _offset_
			- `KOOPA_RVT_STORE` 此指令类型无返回值，需要根据 _from_ 和 _to_ 分情况讨论：从寄存器到全局变量，从寄存器到局部变量，（从寄存器到数组），从内存到全局变量，从内存到局部变量，（从内存到数组），它们的具体指令实现略有不同

	- **编码难点**
		- 生成中间代码时碰到的不只有立即数和临时变量了，还会有局部变量常量、全局变量常量，所以需要增加很多数据结构来记录每种类型的信息。同样生成目标代码时碰到的变数更多，尤其容易搞晕寄存器中存的是内存地址或堆地址的情况，需要脑子清楚地编码
		- 理解栈帧的工作方式，尤其是为后续函数调用的时候打基础

4. 控制流语句
	- **关键功能**
		
		可以解析带有`if_else/while/break/continue`控制流的main函数
	
	- **具体实现方法**
		
		首先我对`if_else`的文法进行了拆解以消除二义性，简单说就是将语句分为了`matched_stmt`和`unmatched_stmt`；在函数 _Koo()_ 中加入了参数 _next_ 代表接下来该执行的代码位置( _BaseAST*_ 类型)。下面是在实现控制流跳转中用到的主要变量：
		```C++
		unordered_map<BaseAST*, int> bbs_begin; // <代码位置，基本块标号>
		unordered_map<int, int> bbs_end; // <基本块标号，下一个基本块标号> 只要是结束了的就为-1
		int bbs_now = 0; // 当前所在基本块
		int bbs_num = 1; // 基本块标号：累加量
		stack<int> while_entry; // 当前基本块的while入口：专给continue用
		stack<int> while_end; // 当前基本块的while出口：专给break用
		```
		采取的方法按照程序执行顺序如下所示：高亮部分代表需要视情况来判定是否生成

		步骤|bbs_now|生成新基本块|bbs_begin改变|bbs_end改变|输出
		-----|--------|-------|--------|-------|------
		碰到if else|  |then_bbs ==next_bbs==|==<next, next_bbs>==|<then_bbs, next_bbs><br>==<next_bbs, bbs_end[bbs_now]>==<br>bbs_end[bbs_now]=-1<br>|branch
		进入then模块| then_bbs | | | |开头
		碰到if then else| |then_bbs else_bbs ==next_bbs==|==<next, next_bbs>==|<then_bbs, next_bbs><br><else_bbs, next_bbs><br>==<next_bbs, bbs_end[bbs_now]>==<br>bbs_end[bbs_now]=-1 |branch
		进入then模块|then_bbs| | |bbs_end[then_bbs]=-1|开头<br>==jump==
		进入else模块|else_bbs| | | |开头
		碰到while| |entry_bbs body_bbs ==next_bbs==|==<next, next_bbs>==<br><this, entry_bbs>|<body_bbs, entry_bbs><br>==<next_bbs, bbs_end[bbs_now]>==<br>bbs_end[bbs_now]=-1|jump
		进入entry模块|entry_bbs| | |bbs_end[entry_bbs]=-1|开头<br>branch
		进入body模块|body_bbs| | | |开头
		
		短路求值的思路和`if_else`是一样的，只是先将1或0存储在`result_reg`中，然后生成`then_bbs else_bbs back_bbs`，只是中间多了将答案存储进`result_reg`的过程。
		
		还有要注意的是，在返回时、遇到 _break_ 时、遇到 _continue_ 时在执行完动作后要设置`bbs_end[bbs_now]=-1`；在碰见新语句块时判断 _this_ 是否在`bbs_begin`中存在，若存在要负责处理当前基本块的结尾+打印新基本块开头+设置`bbs_now`到新基本块。
		
		`riscv_gen.cpp`会碰到新的指令类型`KOOPA_RVT_BRANCH`和`KOOPA_RVT_JUMP`，只需要用哈希表`unordered_map<koopa_raw_basic_block_t, int> BBS`记录一下各个基本块的名字即可。

	- **编码难点**
	
		由于我在写这部分的时候还并未学习控制流的中间语言生成部分，所以写的异常艰难，明明知道如果想清楚了写出来的逻辑应该是优美的，但是由于十分愚蠢导致实现方式非常丑陋，不仅需要维护众多全局变量，还和很多别的不相关部分耦合严重…… _实现一版->出bug->发现自己又晕了->实现一版->出bug->发现自己又晕了……_ 的循环缠绕了我好几周，天天晚上做噩梦TAT
		
5. 函数
	- **关键功能**
		
		可以解析多函数程序
	
	- **具体实现方法**
		
		中间代码生成部分需要注意：
		
		需要记录此函数是否有返回值，从而判断是否需要用寄存器来接 _call_ 函数；对于有参数的函数一开始要重新分配空间并拷贝传进来的值；我选择了将参数的作用域和当前作用域合并，即解析参数时先放到一个专门的地方，进行到真正解析函数时将所有参数（指重新分配后的）合并到当前符号表/作用域内；按照文档加入 _SysY_ 库函数
		
		目标代码生成部分需要注意：
		
		首先在处理到一个函数时，先计算所需栈空间，根据每个指令的有无返回值判断是否累加空间，若碰到 _call_ 指令就为保存 _ra_ 加4，再加上传参预留的空间（具体如下），数组大小后续再说。
		```C++
		auto call_kind = value->kind;
		if(call_kind.tag == KOOPA_RVT_CALL){
			r = 4;
			auto args = call_kind.data.call.args; // koopa_raw_slice_t
			a = max(a, int(args.len));
		}
		```
		随后输出 _prologue_ ，根据情况更新 _sp_ 和保存 _ra_ ；返回时输出 _epilogue_ ，根据情况恢复 _ra_ 和复原 _sp_ ；碰到 _call_ 指令要准备参数传递，即在新碰到的指令类型`KOOPA_RVT_CALL`中将不超过八个的参数按顺序移入寄存器内，超过的按顺序移入高地址方向，返回此参数所在位置，即寄存器或偏移量

	- **编码难点**
		
		暂无逻辑问题，就是要小心编码，几乎每一阶段都会涉及到 _load_ 和 _store_ 指令，而根据不同功能它们的使用都不同，得分情况一个一个讨论
		
6. 数组 
	- **关键功能**
		
		可以解析有数组的多函数程序
	
	- **具体实现方法**
		- 处理初始化：
			概括来说就是当遇到初始化列表时想要判断自己的管辖范围，就取从后往前能整除的最大处截断然后补全0，具体对齐方式如下所示：
			```C++
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
			```
			还有一个很大的改动是在作用域 _Area_ 中除了原本的符号表还加入了数组表和参数数组表，都记录了数组的维度、初始值等
		- 数组使用相关：
			我直接进行了分类讨论：
			
			判断为非函数传参：如果是单变量就拿到其名字 _load_ 出来，常量就返回其值；如果是普通数组就分别执行每个维度的表达式，最后一步步输出 _getelemptr_ 得到最终临时变量；如果是参数数组就得先 _load_ 进地址后调用 _getptr_ ，然后再一步步输出 _getelemptr_
			
			判断为函数传参：如果是单变量和常量则同上；如果是普通数组前期同上，后期对面需要整型参数的话就得 _load_ 出来，需要地址参数的话就得 _getelemptr, 0_ ；如果是参数数组前期同上，后期对面需要整型参数的话调用 _load_ ，需要地址参数的话调用 _getelemptr, 0_
		- 目标代码中的初始化
			
			深度优先搜索拿到当前地址代表的字节大小，搜索函数和调用如下：
			```C++
			int Compute_byte(const koopa_raw_type_t ty){
				if(ty->tag == KOOPA_RTT_INT32)
					return 4;
				if(ty->tag == KOOPA_RTT_ARRAY)
					return ty->data.array.len * Compute_byte(ty->data.array.base);
				return 0;
			}
			int need_byte = Compute_byte(ty->data.pointer.base); // 为了输出.zero sizeof()
			```
		- 处理`KOOPA_RVT_GET_ELEM_PTR`
			
			同样需要分类讨论：如果是全局变量的话先 _la_ 下到 _t0_ ，然后拿到 _offset_ (可能是立即数可能是临时变量)到 _t1_ ，然后拿到上述`need_byte`到 _t2_ ，将 _t1 t2_ 相乘后加 _t0_ ，最后放入内存返回偏移量；如果是指针的话要选择`addi t0, sp, 4`的方式而非 _load_ ，并且应该是由于我实现得不太好，需要更细致地判别这个指针是全局变量还是局部变量变过来的，如果是局部变量还得重新加入<偏移量，此地址存储元素代表的字节大小>。具体的差别很多很多，总结下来分类讨论最主要的原因就是 _getelemptr_ 的源头可能是局部变量、全局变量、内存的一个偏移量，而随着每次 _getelemptr_ 都得更新取完一次后的字节大小
		- 处理`KOOPA_RVT_GET_PTR`
			
			和 _getelemptr_ 不一样的一点是进行完 _getptr_ 操作后拿到的一定是指针，首先不用分大类，其次它也不可能是全局变量变过来的只能是局部变量
			
	- **编码难点**
		
		嗯……全是难点，数组部分写得人非常崩溃，其实在初始化阶段还好，只要想清楚对齐以及慢慢一步步生成中间代码就不会碰到很多bug，但是到了 _getelemptr_ 和 _getptr_ 部分我是头晕目眩，而且由于比较着急没有思考架构而是丑陋地直接加各种细节，使用一堆现在已经看不懂的全局变量而且意义还不明确，最后把所有情况罗列到一大张纸并标清楚每个的指针后才一点点写完的
		
#### 自测试情况
我简单理解为 _debug_ 的过程吧，主要是测试每个 _Lv_ 提供的测试样例，但每当我比较好奇是否正确实现了某个功能时，会先自己构造一个最简单的、几乎只包含这个功能的程序，比如当写完控制流时，我会先写一个几乎包含所有嵌套跳转的 _C_ 程序然后编译；以及到最后数组阶段程序比较复杂时，目标代码完全无法人眼识别错误，我会使用文档提到过的 _编译/运行RISC-V程序_ 来手动更改目标代码再运行，比较方便找到出问题的地方（往往就是指针相关qwq）
#### 存在问题
才疏学浅，并未发现，不好意思

硬说的话文档里有些错别字qwq 比如“基本快”……
## 实习总结
#### 收获与体会
毫无疑问，这是我上大学以来写过的最肝的 _lab_ 没有之一，收获就是编程真的很难，写出优美的工程代码更难……我第一次接触 _libkoopa_ 的时候大为震撼，觉得这种构造真是巧妙得惊为天人，但是到我自己这里：庞大的不知所云的全局变量、混乱的包装得很垃圾的函数、极其严重的功能耦合等等，维护几千行的屎山让我眼前一黑。所以有一个整体的、分布合理的、想清楚的架构观十分重要，要按照功能需求封装好各个部分，而不是面向测试用例不停增加全局变量和分类讨论，真是吃尽了苦水。

其次也提高了我的知识水平（可能也没有？），_C++_ 我本身已经不熟了，只会写简单的数据结构和函数，这个 _lab_ 给我带回来了虚函数、智能指针、强制类型转换等记忆，也让我重新熟悉了深度优先搜索、递归回溯、贪心等有些遗忘的算法，最重要的是 _libkoopa_ 和 _Visit_ 的构造真是太机智了，谢谢 _TA_ !

最后就是想说体验真的是又痛苦又快乐，痛苦在维护我的小垃圾编译器，快乐在我也写出来了个编译器，虽然是个小垃圾。
#### 课程建议
才疏学浅，不好意思，对于 _lab_ 没什么建议，本人也没发现有虫，只想跪地膜拜 _TA_ 哥哥。不过老师的实习课或许可以重新组织一下教学安排？（比如不讲教科书封面或者直接空出来让大家写 _lab_ orz）

最后谢谢老师和助教的辛勤付出！