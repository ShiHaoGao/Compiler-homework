//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//
#include <stdio.h>

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

//using namespace clang;
namespace cl = clang;

class StackFrame {
   /// StackFrame maps Variable Declaration to Value
   /// Which are either integer or addresses (also represented using an Integer value)
   std::map<cl::Decl*, int64_t> mVars;
   std::map<cl::Stmt*, int64_t> mExprs;
   /// The current stmt
   cl::Stmt * mPC;
public:
   StackFrame() : mVars(), mExprs(), mPC() {
   }

   void bindDecl(cl::Decl* decl, int val) {
      mVars[decl] = val;
   }    
   int getDeclVal(cl::Decl * decl) {
      assert (mVars.find(decl) != mVars.end());
      return mVars.find(decl)->second;
   }
   void bindStmt(cl::Stmt * stmt, int val) {
	   mExprs[stmt] = val;
   }
   int getStmtVal(cl::Stmt * stmt) {
	   assert (mExprs.find(stmt) != mExprs.end());
	   return mExprs[stmt];
   }
   void setPC(cl::Stmt * stmt) {
	   mPC = stmt;
   }
   cl::Stmt * getPC() {
	   return mPC;
   }
};

/// Heap maps address to a value
/*
class Heap {
public:
   int Malloc(int size) ;
   void Free (int addr) ;
   void Update(int addr, int val) ;
   int get(int addr);
};
*/

class Environment {
   std::vector<StackFrame> mStack;

   cl::FunctionDecl * mFree;				/// Declartions to the built-in functions
   cl::FunctionDecl * mMalloc;
   cl::FunctionDecl * mInput;
   cl::FunctionDecl * mOutput;

   cl::FunctionDecl * mEntry;
public:
   /// Get the declartions to the built-in functions
   Environment() : mStack(), mFree(NULL), mMalloc(NULL), mInput(NULL), mOutput(NULL), mEntry(NULL) {
   }

   /// Initialize the Environment
   void init(cl::TranslationUnitDecl * unit) {
	   for (cl::TranslationUnitDecl::decl_iterator i =unit->decls_begin(), e = unit->decls_end(); i != e; ++ i) {
		   if (auto * fdecl = dyn_cast<cl::FunctionDecl>(*i) ) {
			   if (fdecl->getName().equals("FREE")) mFree = fdecl;
			   else if (fdecl->getName().equals("MALLOC")) mMalloc = fdecl;
			   else if (fdecl->getName().equals("GET")) mInput = fdecl;
			   else if (fdecl->getName().equals("PRINT")) mOutput = fdecl;
			   else if (fdecl->getName().equals("main")) mEntry = fdecl;
		   }
	   }
	   mStack.push_back(StackFrame());
   }

   cl::FunctionDecl * getEntry() {
	   return mEntry;
   }

   void evalLiteral(cl::Expr *literalExpr) {
       if (auto* literal = cl::dyn_cast<cl::IntegerLiteral>(literalExpr)) {
           mStack.back().bindStmt(literal, literal->getValue().getSExtValue());
       }

   }

   /// !TODO Support comparison operation
   void binop(cl::BinaryOperator *bop) {
	   cl::Expr * left = bop->getLHS();
	   cl::Expr * right = bop->getRHS();

	   if (bop->isAssignmentOp()) {
		   int val = mStack.back().getStmtVal(right);
		   mStack.back().bindStmt(left, val);
		   if (auto * declexpr = dyn_cast<cl::DeclRefExpr>(left)) {
			   cl::Decl * decl = declexpr->getFoundDecl();
			   mStack.back().bindDecl(decl, val);
		   }
	   }
   }

   void decl(cl::DeclStmt * declstmt) {
	   for (cl::DeclStmt::decl_iterator it = declstmt->decl_begin(), ie = declstmt->decl_end();
			   it != ie; ++ it) {
		   cl::Decl * decl = *it;
		   if (auto * vardecl = dyn_cast<cl::VarDecl>(decl)) {
			   mStack.back().bindDecl(vardecl, 0);
		   }
	   }
   }

   void declref(cl::DeclRefExpr * declref) {
	   mStack.back().setPC(declref);
	   if (declref->getType()->isIntegerType()) {
		   cl::Decl* decl = declref->getFoundDecl();

		   int val = mStack.back().getDeclVal(decl);
		   mStack.back().bindStmt(declref, val);
	   }
   }

   void cast(cl::CastExpr * castexpr) {
	   mStack.back().setPC(castexpr);
	   if (castexpr->getType()->isIntegerType()) {
		   cl::Expr * expr = castexpr->getSubExpr();
		   int val = mStack.back().getStmtVal(expr);
		   mStack.back().bindStmt(castexpr, val );
	   }
   }

   /// !TODO Support Function Call
   void call(cl::CallExpr * callexpr) {
	   mStack.back().setPC(callexpr);
	   int val = 0;
	   cl::FunctionDecl * callee = callexpr->getDirectCallee();
	   if (callee == mInput) {
		  llvm::errs() << "Please Input an Integer Value : ";
		  scanf("%d", &val);

		  mStack.back().bindStmt(callexpr, val);
	   } else if (callee == mOutput) {
		   cl::Expr * decl = callexpr->getArg(0);
		   val = mStack.back().getStmtVal(decl);
		   llvm::errs() << val;
	   } else {
		   /// You could add your code here for Function call Return
	   }
   }
};


