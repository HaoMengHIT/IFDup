#include "LockInst.h"
#include <llvm/Pass.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/raw_ostream.h>
#include <sstream>
using namespace std;
using namespace llvm;

char Lock::ID=0;
char Unlock::ID=0;
static RegisterPass<Lock> X("Lock","Lock the instructions");
static RegisterPass<Unlock> Y("Unlock","Unlock the locked instructions");

//Convert int to string
string getString(int tmp)
{
   stringstream newstr;
   newstr<<tmp;
   return newstr.str();
}

//Judge the Type
string judgeType(Type* ty)
{
   string name="";
   Type::TypeID tyid=ty->getTypeID();
   Type* tmp = ty;
   while(tyid == Type::PointerTyID){
      tmp=tmp->getPointerElementType();
      tyid=tmp->getTypeID();
      name+="p";
   }
   /*
   if(tyid==Type::PointerTyID){
      Type* tmp=ty;
      while(tyid == Type::PointerTyID){
         tmp=tmp->getPointerElementType();
         tyid=tmp->getTypeID();
         name+="p";
      }
      if(tyid==Type::IntegerTyID)
         name=name+getString(tyid)+getString(tmp->getPrimitiveSizeInBits());
   }
   */
   if(tyid==Type::IntegerTyID){
      name=name+getString(tyid)+getString(tmp->getPrimitiveSizeInBits());
   }
   else
      name=getString(tyid);
   return name;
}

//Get the name of function in  CallInst
static string getFuncName(Instruction* I,SmallVector<Type*,8>& opty)
{
   string funcname="";
   unsigned size=opty.size();
   funcname+=judgeType(I->getType())+((size>0)?".":"");
   unsigned i;
   for(i=0;i < size-1;i++){
      funcname+=(judgeType(opty[i])+".");
   }
   funcname+=judgeType(opty[i]);
   return funcname;
}

//Lock instructions
void Lock::lock_inst(Instruction *I)
{
   LLVMContext& C = I->getContext();
   Module* M = I->getParent()->getParent()->getParent();
   SmallVector<Type*, 8> OpTypes;
   SmallVector<Value*, 8> OpArgs;
   for(Instruction::op_iterator Op = I->op_begin(), E = I->op_end(); Op!=E; ++Op){
      OpTypes.push_back(Op->get()->getType());
      OpArgs.push_back(Op->get());
   }
   FunctionType* FT = FunctionType::get(I->getType(), OpTypes, false);
   Instruction* T = NULL;
   MDNode* LockMD = MDNode::get(C, MDString::get(C, "IFDup"));
   string nametmp=getFuncName(I,OpTypes);
   if (LoadInst* LI=dyn_cast<LoadInst>(I)){
      Constant* Func = M->getOrInsertFunction("lock.load."+nametmp, FT);
      CallInst* CI = CallInst::Create(Func, OpArgs, "", I);

      unsigned align = LI->getAlignment();
      CI->setMetadata("align."+getString(align), LockMD);
      if(LI->isAtomic())
         CI->setMetadata("atomic."+getString(LI->getOrdering()), LockMD);
      if(LI->isVolatile())
         CI->setMetadata("volatile", LockMD);
      I->replaceAllUsesWith(CI);
      for(unsigned i =0;i < I->getNumOperands();i++)
      {
         I->setOperand(i, UndefValue::get(I->getOperand(i)->getType()));
      }
      I->removeFromParent();
      T = CI;
   }
   else if(StoreInst* SI=dyn_cast<StoreInst>(I)){

   }
   else if(CmpInst* CI=dyn_cast<CmpInst>(I)){

   }
   else if(BinaryOperator* BI=dyn_cast<BinaryOperator>(I)){

   }
   SmallVector<pair<unsigned int, MDNode*>, 8> MDNodes;
   I->getAllMetadata(MDNodes);
   for(unsigned I = 0; I<MDNodes.size(); ++I){
      T->setMetadata(MDNodes[I].first, MDNodes[I].second);
   }
}


#include <llvm/Support/InstIterator.h>
bool Lock::runOnModule(Module &M)
{
   for(Module::iterator F = M.begin(), FE = M.end(); F!=FE; ++F){
      inst_iterator I = inst_begin(F);
      while(I!=inst_end(F)){
         Instruction* self = &*I;
         I++;
         if(isa<LoadInst>(self))
            lock_inst(self);
      }
   }
   return false;
}
bool Unlock::runOnModule(Module &M)
{
   for(Module::iterator F = M.begin(), FE = M.end(); F!=FE; ++F){
      inst_iterator I = inst_begin(F);
      while(I!=inst_end(F)){
         Instruction* self = &*I;
         I++;
         if(isa<CallInst>(self)){
            unlock_inst(self);
         }
      }
   }
   return false;
}

void Unlock::unlock_inst(Instruction* I)
{
   LLVMContext& C = I->getContext();
   CallInst* CI=cast<CallInst>(I);
   SmallVector<Type*, 8>OpTypes;
   SmallVector<Value*, 8>OpArgs;
   for(Instruction::op_iterator Op = I->op_begin(), E = I->op_end(); Op!=E; ++Op){
      OpTypes.push_back(Op->get()->getType());
      OpArgs.push_back(Op->get());
   }
   Function* F=CI->getCalledFunction();
   string cname=F->getName().str();
   SmallVector<pair<unsigned int, MDNode*>, 8> MDNodes;
   CI->getAllMetadata(MDNodes);
   SmallVector<StringRef, 30> names; 
   C.getMDKindNames(names);
   if(cname.find("lock.load") < cname.length()){
      LoadInst* LI=new LoadInst(OpArgs[0],"",I);
      SmallVector<StringRef, 10> tmp;
      for(unsigned i = 0; i < MDNodes.size(); i++){
         // cerr<<names[MDNodes[i].first].str()<<endl;
         names[MDNodes[i].first].split(tmp,".");
         //cerr<<tmp[0]<<"\t"<<endl;
         if(tmp[0]=="volatile")
            LI->setVolatile(true);
         else if(tmp[0]=="atomic"){
            LI->setAtomic((AtomicOrdering)(atoi(tmp[1].str().c_str())));
         }
         else if(tmp[0]=="align")
            LI->setAlignment(atoi(tmp[1].str().c_str()));
         else
            LI->setMetadata(MDNodes[i].first, MDNodes[i].second);
      }
      I->replaceAllUsesWith(LI);
      for(unsigned i =0;i < I->getNumOperands();i++)
      {
         I->setOperand(i, UndefValue::get(I->getOperand(i)->getType()));
      }
      //for(unsigned i = 0; i < MDNodes.size(); i++)
        // MDNodes[i].second->replaceOperandWith(MDNodes[i].first,UndefValue::get(I->getOperand(i)->getType()));
      I->removeFromParent();
      //F->removeFromParent();
      //cerr<<endl;
      errs()<<"found lock.\t"<<cname<<"\n";
   }
   else
      errs()<<"not found lock.\n";
}
