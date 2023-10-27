//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//
#include <stdio.h>

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;
namespace cl = clang;


class StackFrame {
    /// StackFrame maps Variable Declaration to Value
    /// Which are either integer or addresses (also represented using an Integer value)
    // frame中存Decl, Stmt语法树结点 -> value
    std::map<cl::Decl *, int64_t> mVars;
    std::map<cl::Stmt *, int64_t> mExprs;
    std::map<Decl *, std::vector<int64_t>> mArrayVars;
    /// The current stmt
    cl::Stmt *mPC;

public:
    StackFrame() : mVars(), mExprs(), mPC() {
    }

    StackFrame(StackFrame const &sf) {
        mVars = sf.mVars;
        mExprs = sf.mExprs;
        mArrayVars = sf.mArrayVars;
        mPC = sf.mPC;
    }

    StackFrame(StackFrame &&sf) noexcept {
        mVars = std::move(sf.mVars);
        mExprs = std::move(sf.mExprs);
        mArrayVars = std::move(sf.mArrayVars);
        mPC = sf.mPC;
    }


    void bindDecl(cl::Decl *decl, int64_t val) {
        mVars[decl] = val;
    }

    int64_t getDeclVal(cl::Decl *decl) {
        assert(mVars.find(decl) != mVars.end());
        return mVars.find(decl)->second;
    }

    bool hasDeclVal(Decl *decl) const {
        return mVars.find(decl) != mVars.end();
    }

    void bindStmt(cl::Stmt *stmt, int64_t val) {
        mExprs[stmt] = val;
    }

    int64_t getStmtVal(cl::Stmt *stmt) {
        assert(mExprs.find(stmt) != mExprs.end());
        return mExprs[stmt];
    }

    bool hasStmtVal(Stmt *stmt) const {
        return mExprs.find(stmt) != mExprs.end();
    }

    void bindDeclArr(Decl *decl, int size = 0) {
        mArrayVars[decl] = std::vector<int64_t>(size);
    }

    int64_t getDeclArrVar(Decl *decl) {
        assert(mArrayVars.find(decl) != mArrayVars.end());
        return (int64_t) (mArrayVars.find(decl)->second).data();
    }

    //    int64_t getDeclArrVal(Decl* decl, int i) {
    //        assert(mArrayVars.find(decl) != mArrayVars.end());
    //        return mArrayVars[decl][i];
    //    }

    bool hasDeclArr(Decl *decl) const {
        return mArrayVars.find(decl) != mArrayVars.end();
    }


    void setPC(cl::Stmt *stmt) {
        mPC = stmt;
    }

    cl::Stmt *getPC() {
        return mPC;
    }
};

/// Heap maps address to a value

class Heap {
public:
    int Malloc(int size);
    void Free(int addr);
    void Update(int addr, int val);
    int get(int addr);
};


class Environment {
    std::vector<StackFrame> mStack;
    Heap mHeap;
    std::map<Decl *, int64_t> gVals;
    cl::FunctionDecl *mFree;/// Declartions to the built-in functions
    cl::FunctionDecl *mMalloc;
    cl::FunctionDecl *mInput;
    cl::FunctionDecl *mOutput;

    cl::FunctionDecl *mEntry;

public:
    /// Get the declartions to the built-in functions
    Environment() : mStack(), mHeap(), gVals(), mFree(NULL), mMalloc(NULL), mInput(NULL), mOutput(NULL), mEntry(NULL) {
        //       mStack.emplace_back(); // 全局frame
    }

    /// Initialize the Environment
    void init(cl::TranslationUnitDecl *unit) {
        for (cl::TranslationUnitDecl::decl_iterator i = unit->decls_begin(), e = unit->decls_end(); i != e; ++i) {
            if (auto *fdecl = llvm::dyn_cast<cl::FunctionDecl>(*i)) {
                if (fdecl->getName().equals("FREE")) mFree = fdecl;
                else if (fdecl->getName().equals("MALLOC"))
                    mMalloc = fdecl;
                else if (fdecl->getName().equals("GET"))
                    mInput = fdecl;
                else if (fdecl->getName().equals("PRINT"))
                    mOutput = fdecl;
                else if (fdecl->getName().equals("main"))
                    mEntry = fdecl;
            }
            if (auto *vdecl = cl::dyn_cast<cl::VarDecl>(*i)) {

                int64_t val = 0;
                if (cl::Expr *expr = vdecl->getInit()) {
                    if (auto *iLiteral = cl::dyn_cast<cl::IntegerLiteral>(expr)) {
                        val = iLiteral->getValue().getSExtValue();
                    } else if (auto *cLiteral = cl::dyn_cast<cl::CharacterLiteral>(expr)) {
                    }
                    gVals[vdecl] = val;
                    //                   mStack.back().bindDecl(vdecl, val);
                }
            }
        }
        //       mStack.pop_back();
        // main的frame
        mStack.emplace_back();
    }

    cl::FunctionDecl *getEntry() {
        return mEntry;
    }

    void evalLiteral(cl::Expr *literalExpr) {
        if (auto *literal = cl::dyn_cast<cl::IntegerLiteral>(literalExpr)) {
            mStack.back().bindStmt(literal, literal->getValue().getSExtValue());
            //           llvm::errs() << " 1 \n";
        }
    }

    /// !TODO Support comparison operation
    void binop(cl::BinaryOperator *bop) {
        cl::Expr *left = bop->getLHS();
        cl::Expr *right = bop->getRHS();


        int64_t rVal = mStack.back().getStmtVal(right);


        if (bop->isAssignmentOp()) {

//            mStack.back().bindStmt(left, rVal);
            if (auto *declexpr = dyn_cast<cl::DeclRefExpr>(left)) {
                cl::Decl *decl = declexpr->getFoundDecl();
                mStack.back().bindDecl(decl, rVal);
            }
            else if (auto *arrExpr = dyn_cast<ArraySubscriptExpr>(left)) {
                auto* ptr = (int64_t*)mStack.back().getStmtVal(arrExpr->getBase());
                int64_t idx = mStack.back().getStmtVal(arrExpr->getIdx());
                *(ptr + idx) = rVal;
            }
            else if (auto *uop = dyn_cast<UnaryOperator>(left)) {

            }
            return;
        }

        // Computing binary operator
        auto bOpc = bop->getOpcode();
        auto lVal = mStack.back().getStmtVal(left);
        int64_t result = 0;
        switch (bOpc) {
            case BO_Add:
                result = lVal + rVal;
                break;
            case BO_Sub:
                result = lVal - rVal;
                break;
            case BO_Mul:
                result = lVal * rVal;
                break;
            case BO_Div:
                result = lVal / rVal;
                break;
            case BO_LT:
                result = (lVal < rVal) ? 1 : 0;
                break;
            case BO_GT:
                result = (lVal > rVal) ? 1 : 0;
                break;
            case BO_EQ:
                result = (lVal == rVal) ? 1 : 0;
                break;
        }


        mStack.back().bindStmt(bop, result);
    }

    void unOp(UnaryOperator *unop) {
        auto unOpc = unop->getOpcode();
        auto right = unop->getSubExpr();
        auto rVal = mStack.back().getStmtVal(right);

        int64_t result = 0;
        if (unOpc == UO_Minus) {
            result = -rVal;
        }
        mStack.back().bindStmt(unop, result);
    }

    bool isCondTrue(Expr *cond) {
        //        auto cond = ifStmt->getCond();
        int64_t condition = mStack.back().getStmtVal(cond);
        if (condition == 1)
            return true;
        else
            return false;
    }

    void decl(cl::DeclStmt *declstmt) {
        for (cl::DeclStmt::decl_iterator it = declstmt->decl_begin(), ie = declstmt->decl_end();
             it != ie; ++it) {
            cl::Decl *decl = *it;
            if (auto *vardecl = dyn_cast<cl::VarDecl>(decl)) {
                auto type = vardecl->getType();

                // 栈上数组 int a[3];
                if (type->isArrayType()) {
                    auto *array =
                            dyn_cast<ConstantArrayType>(type.getTypePtr());
                    int size = (int) array->getSize().getLimitedValue();
                    mStack.back().bindDeclArr(vardecl, size);
                }
                // 变量 int a, int a = 1, int *a, int *a = malloc();
                // 这四中操作的做法是一样的。
                else if (type->isIntegerType() || type->isPointerType()) {
                    if (vardecl->hasInit()) {
                        mStack.back().bindDecl(
                                vardecl, mStack.back().getStmtVal(vardecl->getInit()));
                    } else {
                        mStack.back().bindDecl(vardecl, 0);// 新定义的变量初始化为 0
                    }
                } else {
                    llvm::errs() << "Unknown decl. \n";
                }
            }
        }
    }

    void declref(cl::DeclRefExpr *declref) {
        auto type = declref->getType();
        // 对变量声明的引用，在本作用域，或者在全局作用域。
        if (type->isIntegerType() || type->isPointerType()) {
            auto *decl = declref->getFoundDecl();
            int64_t val = 0;
            // 当前frame
            if (mStack.back().hasDeclVal(decl))
                val = mStack.back().getDeclVal(decl);
            // 全局gVal
            else if (gVals.find(decl) != gVals.end())
                val = gVals[decl];
            else
                llvm::errs() << "Can't find declVal in current frame's mVals and gVals!\n";
            mStack.back().bindStmt(declref, val);
        } else if (type->isConstantArrayType()) {
            auto *decl = declref->getFoundDecl();
            int64_t ptr = 0;
            // 如果栈上存在这个数组的decl
            // 也就是说，这个decl在mArrayVars中，那么用栈上的数组地址
            if (mStack.back().hasDeclArr(decl))
                ptr = mStack.back().getDeclArrVar(decl);
            // else：数组的数据内容在堆上，
            else if (mStack.back().hasDeclVal(decl)) {
                ptr = mStack.back().getDeclVal(decl);
            } else {
                llvm::errs() << "Can't ref array on stack and heap. \n";
            }
            mStack.back().bindStmt(declref, ptr);
        }
    }


    // 这里无法区分数组地址指向的内容是在栈上还是堆上
    // 将a[i]视为右值。没有将地址加入mExprs中
    // 因此，在a[i]视为左值时，也就是assign的时候，需要重新计算地址。
    void arraySubscript(ArraySubscriptExpr *arrayExpr) {
        auto *base = arrayExpr->getBase();
        auto *arr = (int64_t *) mStack.back().getStmtVal(base);
        int64_t idx = mStack.back().getStmtVal(arrayExpr->getIdx());
        int64_t val = *(arr + idx);
        mStack.back().bindStmt(arrayExpr, val);
    }

    void cast(cl::CastExpr *castexpr) {
        auto type = castexpr->getType();
        // 这里必须把functionPointer去掉，因为对于调用的函数，我们并没有将其expr加入到mExpr中
        // 而是直接控制ast树的访问顺序，直接去解释执行其函数。
        // 与编译器中，将函数视为指针，也保存在expr中是不同的。
        // 究其原因是因为我们写的是解释器，而不是编译器
        if (type->isFunctionPointerType())
            return ;
        if (type->isIntegerType() || type->isPointerType() || type->isConstantArrayType()) {
            cl::Expr *expr = castexpr->getSubExpr();
            int64_t val = mStack.back().getStmtVal(expr);
            mStack.back().bindStmt(castexpr, val);
        }
        //        else if (castexpr->getType()->isArrayType()) {
        //            auto expr = castexpr->getSubExpr();
        //
        //        }
    }

    bool isBuiltinCall(CallExpr *callExpr) {
        FunctionDecl *callee = callExpr->getDirectCallee();
        return callee == mInput || callee == mOutput || callee == mFree || callee == mMalloc;
    }

    void builtinCall(CallExpr *callExpr) {
        assert(isBuiltinCall(callExpr) == true);
        FunctionDecl *callee = callExpr->getDirectCallee();
        if (callee == mInput) {
            int val = 0;
            llvm::errs() << "Please Input an Integer Value : ";
            scanf("%d", &val);
            mStack.back().bindStmt(callExpr, val);
        } else if (callee == mOutput) {
            int64_t val = 0;
            cl::Expr *decl = callExpr->getArg(0);
            val = mStack.back().getStmtVal(decl);
            llvm::errs() << val;
        }
    }

    /// !TODO Support Function Call
    void call(cl::CallExpr *callExpr) {
        assert(isBuiltinCall(callExpr) == false);
        FunctionDecl *callee = callExpr->getDirectCallee();
        // 新建Frame
        auto *newFrame = new StackFrame();
        // bind parameters
        unsigned argsNum = callExpr->getNumArgs();
        for (unsigned i = 0; i < argsNum; ++i) {
            Decl *param = callee->getParamDecl(i);
            Expr *arg = callExpr->getArg(i);
            int64_t val = mStack.back().getStmtVal(arg);
            newFrame->bindDecl(param, val);
        }
        // 设置新Frame的返回pc值
        newFrame->setPC(callExpr);
        // 开return val的空间
        // Expr是ValueStmt，包含type和value的stmt，因此这里用bindStmt。
        newFrame->bindStmt(callExpr, 0);
        // 将newFrame 压入Stack中。
        mStack.push_back(*newFrame);
    }

    // 活动记录结束
    void freeFrame(CallExpr *callExpr) {
        // 拿到返回值
        int64_t val = mStack.back().getStmtVal(callExpr);

        // 释放frame
        mStack.pop_back();
        mStack.back().bindStmt(callExpr, val);
        //        llvm::errs() << "bind val : " << val << "\n";
    }

    void callReturn(ReturnStmt *returnStmt) {
        // 拿到返回pc值，需要将返回值bind到pc的stmt上，
        auto *callExpr = mStack.back().getPC();
        // 拿到返回值
        auto expr = returnStmt->getRetValue();
        auto val = mStack.back().getStmtVal(expr);

        // 进行bind pc -> val
        // 如果是main，则不做这个操作，因为没有给main frame设置pc值。
        if (mStack.size() != 1) {
            int tmp = mStack.back().getStmtVal(callExpr);
            //        llvm::errs() << "return val : " << tmp << "\n";
            mStack.back().bindStmt(callExpr, val);
            //        llvm::errs() << "return val : " << val << "\n";
        }

    };
};

class ReturnException : public std::exception {
};
