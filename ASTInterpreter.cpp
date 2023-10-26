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

class InterpreterVisitor : 
public clang::EvaluatedExprVisitor<InterpreterVisitor> {
public:
   explicit InterpreterVisitor(const clang::ASTContext &context, Environment * env)
   : EvaluatedExprVisitor(context), mEnv(env) {}
   virtual ~InterpreterVisitor() {}

   virtual void VisitBinaryOperator (cl::BinaryOperator * bop) {
	   VisitStmt(bop);
	   mEnv->binop(bop);
   }
   virtual void VisitDeclRefExpr(cl::DeclRefExpr * expr) {
	   VisitStmt(expr);
	   mEnv->declref(expr);
   }

    virtual void VisitIntegerLiteral(cl::IntegerLiteral *literal) {
        mEnv->evalLiteral(literal);
    }

   virtual void VisitCastExpr(cl::CastExpr * expr) {
	   VisitStmt(expr);
	   mEnv->cast(expr);
   }

   virtual void VisitCallExpr(clang::CallExpr * call) {
	   VisitStmt(call);
	   mEnv->call(call);
       auto * body = call->getDirectCallee()->getBody();
       // 转移控制
       VisitStmt(body);
       // 控制返回，提取参数，清除frame
       mEnv->freeFrame(call);
   }

   virtual void VisitDeclStmt(DeclStmt * declstmt) {
       VisitStmt(declstmt);
	   mEnv->decl(declstmt);
   }

   virtual void VisitReturnStmt(ReturnStmt* returnStmt) {
       VisitStmt(returnStmt);
       mEnv->callReturn(returnStmt);
   }

private:
   Environment * mEnv;
};

class InterpreterConsumer : public clang::ASTConsumer {
public:
    explicit InterpreterConsumer(const clang::ASTContext& context) : mEnv(),
   	   mVisitor(context, &mEnv) {
    }
    virtual ~InterpreterConsumer() {}

    void HandleTranslationUnit(clang::ASTContext &Context) override {
	    clang::TranslationUnitDecl * decl = Context.getTranslationUnitDecl();

       mEnv.init(decl); // 把runtime的函数和main函数都装入Env中，然后开一个main的frame。

	   clang::FunctionDecl * entry = mEnv.getEntry();
//       mVisitor.VisitStmt(entry->getBody());

        try {
            mVisitor.VisitStmt(entry->getBody());
        } catch (ReturnException e) {
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

int main (int argc, char ** argv) {
   if (argc > 1) {
       clang::tooling::runToolOnCode(std::unique_ptr<clang::FrontendAction>(new InterpreterClassAction), argv[1]);
   }
}
