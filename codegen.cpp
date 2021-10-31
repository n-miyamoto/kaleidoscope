#include "codegen.hpp"

#include "KaleidoscopeJIT.h"
#include "expressions.hpp"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"

static llvm::Value *LogErrorV(const char *Str) {
  LogError(Str);
  return nullptr;
}

CodeGenVisitor::CodeGenVisitor() {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  TheJIT = std::make_unique<llvm::orc::KaleidoscopeJIT>();
  InitializeModuleAndPassManager();
}

void CodeGenVisitor::InitializeModuleAndPassManager() {
  // Open a new context and module.
  TheContext = std::make_unique<llvm::LLVMContext>();
  TheModule = std::make_unique<llvm::Module>("my cool jit", *TheContext);
  TheModule->setDataLayout(TheJIT->getTargetMachine().createDataLayout());

  // Create a new builder for the module.
  Builder = std::make_unique<llvm::IRBuilder<>>(*TheContext);

  // Create a new pass manager
  TheFPM = std::make_unique<llvm::legacy::FunctionPassManager>(TheModule.get());
  // add path
  TheFPM->add(llvm::createInstructionCombiningPass());
  TheFPM->add(llvm::createReassociatePass());
  TheFPM->add(llvm::createGVNPass());
  TheFPM->add(llvm::createCFGSimplificationPass());

  TheFPM->doInitialization();
}

llvm::Function *CodeGenVisitor::getFunction(std::string Name) {
  // First, see if the function has already been added to the current module.
  if (auto *F = TheModule->getFunction(Name)) return F;

  // If not, check whether we can codegen the declaration from some existing
  // prototype.
  auto FI = FunctionProtos.find(Name);
  if (FI != FunctionProtos.end())
    // return FI->second->codegen();
    return FI->second->Accept(*this);

  // If no existing prototype exists, return null.
  return nullptr;
}

llvm::Function *CodeGenVisitor::Visit(PrototypeAST &p) {
  std::vector<llvm::Type *> Doubles(p.Args.size(),
                                    llvm::Type::getDoubleTy(*TheContext));

  llvm::FunctionType *FT = llvm::FunctionType::get(
      llvm::Type::getDoubleTy(*TheContext), Doubles, false);

  llvm::Function *F = llvm::Function::Create(
      FT, llvm::Function::ExternalLinkage, p.Name, TheModule.get());

  unsigned Idx = 0;
  for (auto &Arg : F->args()) {
    Arg.setName(p.Args[Idx++]);
  }

  return F;
}

// code gen impl
llvm::Function *CodeGenVisitor::Visit(FunctionAST &f) {
  auto &P = *(f.Proto);
  FunctionProtos[f.Proto->getName()] = std::move(f.Proto);
  llvm::Function *TheFunction = getFunction(P.getName());
  if (!TheFunction) return nullptr;

  if (!TheFunction)
    // TheFunction = f.Proto->codegen();
    TheFunction = f.Accept(*this);

  if (!TheFunction) return nullptr;

  if (!TheFunction->empty())
    return (llvm::Function *)LogErrorV("Function cannot be redefined.");

  llvm::BasicBlock *BB =
      llvm::BasicBlock::Create(*TheContext, "entry", TheFunction);
  Builder->SetInsertPoint(BB);

  NamedValues.clear();
  for (auto &arg : TheFunction->args()) {
    NamedValues[arg.getName()] = &arg;
  }

  // if(llvm::Value *RetVal = f.Body->codegen()){
  if (llvm::Value *RetVal = f.Body->Accept(*this)) {
    Builder->CreateRet(RetVal);
    llvm::verifyFunction(*TheFunction);

    // Optimize the function.
    TheFPM->run(*TheFunction);
    return TheFunction;
  }

  TheFunction->eraseFromParent();
  return nullptr;
}

llvm::Value *CodeGenVisitor::Visit(NumberExprAST &n) {
  return llvm::ConstantFP::get(*TheContext, llvm::APFloat(n.Val));
}
llvm::Value *CodeGenVisitor::Visit(VariableExprAST &v) {
  // Look this variable up in the function.
  llvm::Value *V = NamedValues[v.Name];
  if (!V) LogErrorV("Unknown variable name");
  return V;
}

llvm::Value *CodeGenVisitor::Visit(BinaryExprAST &b) {
  // llvm::Value *L = b.LHS->codegen();
  llvm::Value *L = b.LHS->Accept(*this);
  // llvm::Value *R = b.RHS->codegen();
  llvm::Value *R = b.RHS->Accept(*this);
  if (!L || !R) return nullptr;

  switch (b.Op) {
    case '+':
      return Builder->CreateFAdd(L, R, "addtmp");
    case '-':
      return Builder->CreateFSub(L, R, "subtmp");
    case '*':
      return Builder->CreateFMul(L, R, "multmp");
    case '<':
      L = Builder->CreateFCmpULT(L, R, "cmptmp");
      // Convert bool 0/1 to double 0.0 or 1.0
      return Builder->CreateUIToFP(L, llvm::Type::getDoubleTy(*TheContext),
                                   "booltmp");
    default:
      return LogErrorV("invalid binary operator");
  }
}
llvm::Value *CodeGenVisitor::Visit(CallExprAST &c) {
  llvm::Function *CalleeF = getFunction(c.Callee);
  // llvm::Function *CalleeF = TheModule-> getFunction(Callee);
  if (!CalleeF) return LogErrorV("Unknown function referenced");
  if (CalleeF->arg_size() != c.Args.size())
    return LogErrorV("Incorrect #arguments passed");

  std::vector<llvm::Value *> ArgsV;
  for (unsigned i = 0, e = c.Args.size(); i != e; i++) {
    // ArgsV.push_back( c.Args[i]->codegen() );
    ArgsV.push_back(c.Args[i]->Accept(*this));
    if (!ArgsV.back()) return nullptr;
  }
  return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

llvm::Value *CodeGenVisitor::Visit(IfExprAST &i) {
  llvm::Value *CondV = i.Cond->Accept(*this);
  if (!CondV) return nullptr;

  // Convert condition to a bool by comparing non-equal to 0.0.
  CondV = Builder->CreateFCmpONE(
      CondV, llvm::ConstantFP::get(*TheContext, llvm::APFloat(0.0)), "ifcond");
  llvm::Function *TheFunction = Builder->GetInsertBlock()->getParent();

  // Create blocks for the then and else cases.  Insert the 'then' block at the
  // end of the function.
  llvm::BasicBlock *ThenBB =
      llvm::BasicBlock::Create(*TheContext, "then", TheFunction);
  llvm::BasicBlock *ElseBB = llvm::BasicBlock::Create(*TheContext, "else");
  llvm::BasicBlock *MergeBB = llvm::BasicBlock::Create(*TheContext, "ifcont");

  Builder->CreateCondBr(CondV, ThenBB, ElseBB);

  // Emit then value.
  Builder->SetInsertPoint(ThenBB);

  llvm::Value *ThenV = i.Then->Accept(*this);
  if (!ThenV) return nullptr;

  Builder->CreateBr(MergeBB);
  // Codegen of 'Then' can change the current block, update ThenBB for the PHI.
  ThenBB = Builder->GetInsertBlock();

  // Emit else block.
  TheFunction->getBasicBlockList().push_back(ElseBB);
  Builder->SetInsertPoint(ElseBB);

  llvm::Value *ElseV = i.Else->Accept(*this);
  if (!ElseV) return nullptr;

  Builder->CreateBr(MergeBB);
  // codegen of 'Else' can change the current block, update ElseBB for the PHI.
  ElseBB = Builder->GetInsertBlock();

  // Emit merge block.
  TheFunction->getBasicBlockList().push_back(MergeBB);
  Builder->SetInsertPoint(MergeBB);
  llvm::PHINode *PN =
      Builder->CreatePHI(llvm::Type::getDoubleTy(*TheContext), 2, "iftmp");

  PN->addIncoming(ThenV, ThenBB);
  PN->addIncoming(ElseV, ElseBB);
  return PN;

  return nullptr;
}

llvm::Value *CodeGenVisitor::Visit(ForExprAST &f) {
  // Emit the start code first, without 'variable' in scope.
  llvm::Value *StartVal = f.Start->Accept(*this);
  if (!StartVal)
    return nullptr;

  // Make the new basic block for the loop header, inserting after current
  // block.
  llvm::Function *TheFunction = Builder->GetInsertBlock()->getParent();
  llvm::BasicBlock *PreheaderBB = Builder->GetInsertBlock();
  llvm::BasicBlock *LoopBB = llvm::BasicBlock::Create(*TheContext, "loop", TheFunction);

  // Insert an explicit fall through from the current block to the LoopBB.
  Builder->CreateBr(LoopBB);

  // Start insertion in LoopBB.
  Builder->SetInsertPoint(LoopBB);

  // Start the PHI node with an entry for Start.
  llvm::PHINode *Variable = Builder->CreatePHI(llvm::Type::getDoubleTy(*TheContext), 2, f.VarName.c_str());
  Variable->addIncoming(StartVal, PreheaderBB);

  // Within the loop, the variable is defined equal to the PHI node.  If it
  // shadows an existing variable, we have to restore it, so save it now.
  llvm::Value *OldVal = NamedValues[f.VarName];
  NamedValues[f.VarName] = Variable;

  // Emit the body of the loop.  This, like any other expr, can change the
  // current BB.  Note that we ignore the value computed by the body, but don't
  // allow an error.
  if (!f.Body->Accept(*this))
    return nullptr;

  // Emit the step value.
  llvm::Value *StepVal = nullptr;
  if (f.Step) {
    StepVal = f.Step->Accept(*this);
    if (!StepVal)
      return nullptr;
  } else {
    // If not specified, use 1.0.
    StepVal = llvm::ConstantFP::get(*TheContext, llvm::APFloat(1.0));
  }

  llvm::Value *NextVar = Builder->CreateFAdd(Variable, StepVal, "nextvar");

  // Compute the end condition.
  llvm::Value *EndCond = f.End->Accept(*this);
  if (!EndCond)
    return nullptr;

  // Convert condition to a bool by comparing non-equal to 0.0.
  EndCond = Builder->CreateFCmpONE(
      EndCond, llvm::ConstantFP::get(*TheContext, llvm::APFloat(0.0)), "loopcond");

  // Create the "after loop" block and insert it.
  llvm::BasicBlock *LoopEndBB = Builder->GetInsertBlock();
  llvm::BasicBlock *AfterBB = llvm::BasicBlock::Create(*TheContext, "afterloop", TheFunction);

  // Insert the conditional branch into the end of LoopEndBB.
  Builder->CreateCondBr(EndCond, LoopBB, AfterBB);

  // Any new code will be inserted in AfterBB.
  Builder->SetInsertPoint(AfterBB);

  // Add a new entry to the PHI node for the backedge.
  Variable->addIncoming(NextVar, LoopEndBB);

  // Restore the unshadowed variable.
  if (OldVal)
    NamedValues[f.VarName] = OldVal;
  else
    NamedValues.erase(f.VarName);

  // for expr always returns 0.0.
  return llvm::Constant::getNullValue(llvm::Type::getDoubleTy(*TheContext));
}