//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

//using namespace clang;
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
   }
   virtual void VisitDeclStmt(clang::DeclStmt * declstmt) {
       VisitStmt(declstmt);
	   mEnv->decl(declstmt);
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
	   mEnv.init(decl);

	   clang::FunctionDecl * entry = mEnv.getEntry();
	   mVisitor.VisitStmt(entry->getBody());
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
