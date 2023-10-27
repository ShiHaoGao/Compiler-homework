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
    /// The current stmt
    cl::Stmt *mPC;

public:
    StackFrame() : mVars(), mExprs(), mPC() {
    }

    StackFrame(StackFrame const &sf) {
        mVars = sf.mVars;
        mExprs = sf.mExprs;
        mPC = sf.mPC;
    }

    StackFrame(StackFrame &&sf) {
        mVars = std::move(sf.mVars);
        mExprs = std::move(sf.mExprs);
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

        auto bOpc = bop->getOpcode();

        if (bop->isAssignmentOp()) {
            int val = mStack.back().getStmtVal(right);
            mStack.back().bindStmt(left, val);
            if (auto *declexpr = dyn_cast<cl::DeclRefExpr>(left)) {
                cl::Decl *decl = declexpr->getFoundDecl();
                mStack.back().bindDecl(decl, val);
            }
            return;
        }

        // Computing binary operator
        auto lVal = mStack.back().getStmtVal(left);
        auto rVal = mStack.back().getStmtVal(right);
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
                result = (lVal < rVal) ? 1:0;
                break;
            case BO_GT:
                result = (lVal > rVal) ? 1:0;
                break;
            case BO_EQ:
                result = (lVal == rVal) ? 1:0;
                break;
        }


        mStack.back().bindStmt(bop, result);


    }

    void unOp(UnaryOperator* unop) {
        auto unOpc = unop->getOpcode();
        auto right = unop->getSubExpr();
        auto rVal = mStack.back().getStmtVal(right);

        int64_t result = 0;
        if (unOpc == UO_Minus) {
            result = -rVal;
        }
        mStack.back().bindStmt(unop, result);
    }

    bool isCondTrue(Expr* cond) {
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
                if (vardecl->hasInit()) {
                    mStack.back().bindDecl(
                            vardecl, mStack.back().getStmtVal(vardecl->getInit()));
                } else {
                    mStack.back().bindDecl(vardecl, 0);// 新定义的变量初始化为 0
                }
            }
        }
    }

    void declref(cl::DeclRefExpr *declref) {
        // 对变量声明的引用，在本作用域，或者在全局作用域。
        if (declref->getType()->isIntegerType()) {
            cl::Decl *decl = declref->getFoundDecl();
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
        }
    }

    void cast(cl::CastExpr *castexpr) {
        if (castexpr->getType()->isIntegerType()) {
            cl::Expr *expr = castexpr->getSubExpr();
            int val = mStack.back().getStmtVal(expr);
            mStack.back().bindStmt(castexpr, val);
        }
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
        int tmp = mStack.back().getStmtVal(callExpr);
        //        llvm::errs() << "return val : " << tmp << "\n";
        mStack.back().bindStmt(callExpr, val);
        //        llvm::errs() << "return val : " << val << "\n";
    };
};

class ReturnException : public std::exception {
};
