//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;
namespace cl = clang;

#include "Environment.h"

class ReturnException : std::exception {
};

class InterpreterVisitor : public clang::EvaluatedExprVisitor<InterpreterVisitor> {
public:
    explicit InterpreterVisitor(const clang::ASTContext &context, Environment *env)
        : EvaluatedExprVisitor(context), mEnv(env) {}
    virtual ~InterpreterVisitor() {}

    virtual void VisitBinaryOperator(cl::BinaryOperator *bop) {
        VisitStmt(bop);
        mEnv->binop(bop);
    }

    virtual void VisitUnaryOperator(UnaryOperator *unop) {
        VisitStmt(unop);
        mEnv->unOp(unop);
    }

    virtual void VisitParenExpr(ParenExpr *parenExpr) {
        VisitStmt(parenExpr);
        mEnv->evalParenthesis(parenExpr);
    }

    virtual void VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *sizeofExpr) {
        VisitStmt(sizeofExpr);
        mEnv->evalSizeof(sizeofExpr);
    }

    virtual void VisitDeclRefExpr(cl::DeclRefExpr *expr) {
        VisitStmt(expr);
        mEnv->declref(expr);
    }

    virtual void VisitArraySubscriptExpr(ArraySubscriptExpr *expr) {
        VisitStmt(expr);
        mEnv->arraySubscript(expr);
    }

    virtual void VisitIntegerLiteral(cl::IntegerLiteral *literal) {
        mEnv->evalLiteral(literal);
    }

    virtual void VisitCastExpr(cl::CastExpr *expr) {
        VisitStmt(expr);
        mEnv->cast(expr);
    }

    virtual void VisitCallExpr(clang::CallExpr *call) {
        VisitStmt(call);
        if (mEnv->isBuiltinCall(call)) {
            mEnv->builtinCall(call);
            return;
        }

        // 非 builtin call，要建立newframe
        mEnv->call(call);

        auto *body = call->getDirectCallee()->getBody();
        try {
            // 转移控制
            if (body)
                VisitStmt(body);
        } catch (ReturnException &e) {
        }

        // 控制返回，提取参数，清除frame
        mEnv->freeFrame(call);
    }

    virtual void VisitDeclStmt(DeclStmt *declstmt) {
        VisitStmt(declstmt);
        mEnv->decl(declstmt);
    }

    virtual void VisitReturnStmt(ReturnStmt *returnStmt) {
        VisitStmt(returnStmt);
        mEnv->callReturn(returnStmt);
        throw ReturnException();
    }

    virtual void VisitIfStmt(IfStmt *ifStmt) {
        auto cond = ifStmt->getCond();
        Visit(cond);

        if (mEnv->isCondTrue(cond)) {
            auto thenStmt = ifStmt->getThen();
            // 如果存在then stmt，则访问
            if (thenStmt)
                Visit(thenStmt);
        } else {
            auto elseStmt = ifStmt->getElse();
            // 如果存在else stmt，则访问
            if (elseStmt)
                Visit(elseStmt);
        }
    }

    virtual void VisitWhileStmt(WhileStmt *whileStmt) {

        auto cond = whileStmt->getCond();
        Visit(cond);
        while (mEnv->isCondTrue(cond)) {
            if (auto *whileBody = whileStmt->getBody())
                Visit(whileBody);
            Visit(cond);
        }
    }

    virtual void VisitForStmt(ForStmt *forStmt) {
        auto cond = forStmt->getCond();

        auto inc = forStmt->getInc();
        if (auto init = forStmt->getInit())
            Visit(init);
        if (cond)
            Visit(cond);
        while (mEnv->isCondTrue(cond)) {
            if (auto body = forStmt->getBody())
                Visit(body);
            else
                break;
            if (inc) Visit(inc);
            if (cond) Visit(cond);
        }
    }

private:
    Environment *mEnv;
};

class InterpreterConsumer : public clang::ASTConsumer {
public:
    explicit InterpreterConsumer(const clang::ASTContext &context) : mEnv(),
                                                                     mVisitor(context, &mEnv) {
    }
    virtual ~InterpreterConsumer() {}

    void HandleTranslationUnit(clang::ASTContext &Context) override {
        clang::TranslationUnitDecl *decl = Context.getTranslationUnitDecl();

        mEnv.init(decl);// 把runtime的函数和main函数都装入Env中，然后开一个main的frame。
        FunctionDecl *entry = mEnv.getEntry();
        try {
            mVisitor.VisitStmt(entry->getBody());
        } catch (ReturnException &r) {
        }
    }

private:
    Environment mEnv;
    InterpreterVisitor mVisitor;
};

class InterpreterClassAction : public clang::ASTFrontendAction {
public:
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
            clang::CompilerInstance &Compiler, llvm::StringRef InFile) override {
        return std::unique_ptr<clang::ASTConsumer>(
                new InterpreterConsumer(Compiler.getASTContext()));
    }
};

int main(int argc, char **argv) {
    if (argc > 1) {
        clang::tooling::runToolOnCode(std::unique_ptr<clang::FrontendAction>(new InterpreterClassAction), argv[1]);
    }
}
