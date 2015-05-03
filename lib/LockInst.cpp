//===--LockInst.cpp-------*-C++ -*-====================//
//The file implements the lock and unlock
//of Instruction like LoadInst,StoreInst,
//CmpInst and BinaryOperator.
//=====================================================//
#include "LockInst.h"
#include <llvm/Pass.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/InstIterator.h>

#include <sstream>
#include <time.h>

#include "debug.h"

//using namespace std;
using namespace llvm;

char Lock::ID=0;
char Unlock::ID=0;
static RegisterPass<Lock> X("Lock","provide ability to lock the instructions");
static RegisterPass<Unlock> Y("Unlock","Unlock the locked instructions");

//convert int to string
static std::string getString(int tmp)
{
   std::stringstream newstr;
   newstr<<tmp;
   return newstr.str();
}

//judge the type of args of instruction
static std::string judgeType(Type* ty)
{
   std::string name="";
   Type::TypeID tyid=ty->getTypeID();
   Type* tmp = ty;
   while(tyid == Type::PointerTyID){
      tmp=tmp->getPointerElementType();
      tyid=tmp->getTypeID();
      name+="p";
   }
   if(tyid==Type::IntegerTyID){
      name=name+getString(tyid)+getString(tmp->getPrimitiveSizeInBits());
   }
   else if(tyid==Type::StructTyID){
      name=name+getString(tyid)+tmp->getStructName().str();
   }
   else if(tyid==Type::FunctionTyID){
      std::string tmpname = "";
      for(unsigned i = 0;i < tmp->getFunctionNumParams();i++)
         tmpname += getString(tmp->getFunctionParamType(i)->getTypeID());
      //add the clock number to funciton name
      name = name+getString(tyid)+tmpname+getString(clock());
   }
   else
      name=name+getString(tyid)+getString(clock());
   return name;
}

//get the name of CallInst
static std::string getFuncName(Instruction* I,SmallVectorImpl<Type*>& opty)
{
   std::string funcname="";
   unsigned size=opty.size();
   funcname+=judgeType(I->getType())+((size>0)?".":"");
   unsigned i;
   for(i=0;i < size-1;i++){
      funcname+=(judgeType(opty[i])+".");
   }
   funcname+=judgeType(opty[i]);
   return funcname;
}

//lock_inst()
//convert the instruction to CallInst,that is the Lock of instruction
Instruction* Lock::lock_inst(Instruction *I)
{

   //DEBUG(errs()<<"ins start: "<<*I<<"\n");

   LLVMContext& C = I->getContext();
   Module* M = I->getParent()->getParent()->getParent();
   SmallVector<Type*, 8> OpTypes;
   SmallVector<Value*, 8> OpArgs;
   for(Instruction::op_iterator Op = I->op_begin(), E = I->op_end(); Op!=E; ++Op){
      OpTypes.push_back(Op->get()->getType());
      OpArgs.push_back(Op->get());
   }
   
   Type* returnType  = I->getType();
   FunctionType* FT = FunctionType::get(returnType, OpTypes, false);
   CallInst* CI = NULL;
   Constant* Func = NULL; 
   MDNode* LockMD = MDNode::get(C, MDString::get(C, "IFDup"));
   std::string nametmp=getFuncName(I,OpTypes);
   
   //DEBUG(errs()<<"Functin name: "<<nametmp<<"\n");
   //DEBUG(errs()<<"FunctionType: "<<*FT<<"\n");
   //DEBUG(errs()<<"ReturnType: "<<*returnType<<"\n");
   //Lock LoadInst
   if (LoadInst* LI=dyn_cast<LoadInst>(I)){
      Func = M->getOrInsertFunction("lock.load."+nametmp, FT);
      CI = CallInst::Create(Func, OpArgs, "", I);

      unsigned align = LI->getAlignment();
      CI->setMetadata("align."+getString(align), LockMD);
      if(LI->isAtomic())
         CI->setMetadata("atomic."+getString(LI->getOrdering())+"."+getString(LI->getSynchScope()), LockMD);
      if(LI->isVolatile())
         CI->setMetadata("volatile", LockMD);
   }

   //Lock StoreInst
   else if(StoreInst* SI=dyn_cast<StoreInst>(I)){
      Func = M->getOrInsertFunction("lock.store."+nametmp, FT);
      CI = CallInst::Create(Func, OpArgs, "", I);
      unsigned align = SI->getAlignment();
      CI->setMetadata("align."+getString(align), LockMD);
      if(SI->isVolatile())
         CI->setMetadata("volatile", LockMD);
      if(SI->isAtomic())
         CI->setMetadata("atomic."+getString(SI->getOrdering())+"."+getString(SI->getSynchScope()), LockMD);
   }

   //Lock CmpInst
   else if(CmpInst* CMI=dyn_cast<CmpInst>(I)){
      Func = M->getOrInsertFunction("lock.cmp."+getString(CMI->getOpcode())+"."+getString(CMI->getPredicate())+"."+nametmp, FT);
      CI = CallInst::Create(Func, OpArgs, "", I);
   }

   //Lock BinaryOperator
   else if(BinaryOperator* BI=dyn_cast<BinaryOperator>(I)){
      StringRef opCodeName=StringRef(BI->getOpcodeName());
      //DEBUG(errs()<<"hello world!\n");
      //DEBUG(errs()<<opCodeName.str()<<"\n");
      Func = M->getOrInsertFunction("lock.BinaryOp."+opCodeName.str()+"."+getString(BI->getOpcode())+"."+nametmp, FT);
      CI = CallInst::Create(Func, OpArgs, "", I);
      if(BI->hasNoUnsignedWrap())
         CI->setMetadata("nuw", LockMD);
      if(BI->hasNoSignedWrap())
         CI->setMetadata("nsw", LockMD);
      if(BI->isExact())
         CI->setMetadata("exact", LockMD);
   }
   //Lock CastInst
   else if(CastInst* Cast=dyn_cast<CastInst>(I))
   {
      StringRef opCodeName = StringRef(Cast->getOpcodeName());
      Func = M->getOrInsertFunction("lock.CastInst."+opCodeName.str()+"."+getString(Cast->getOpcode())+"."+nametmp, FT);
      CI = CallInst::Create(Func, OpArgs, "", I);
   }
   //Lock GetElementPtrInst
   else if(GetElementPtrInst* GEP=dyn_cast<GetElementPtrInst>(I))
   {
      std::string inbounds="no-inbounds.";
      if(GEP->isInBounds())
      {
         inbounds="inbounds.";
      }
      Func = M->getOrInsertFunction("lock.getelementptrinst."+inbounds+nametmp, FT);
      CI = CallInst::Create(Func, OpArgs, "", I);
   }
   else if(isa<BranchInst>(I)||isa<PHINode>(I)){
      return I;
   }else{
      return I;
      //Assert(0, "unknow inst type"<<*I);
   }
   //Debug the invoke 
   //CI->setDoesNotReturn();
   CI->setDoesNotThrow();

   I->replaceAllUsesWith(CI);

   //DEBUG(errs()<<"Func: "<<*Func<<"\n");
   DEBUG(errs()<<"LockInst.cpp CI: "<<*CI<<"\n\n\n");
   //Set the operands of instruction to UndefValue, otherwise it will be wrong when using I->removeFromParent()
   for(unsigned i =0;i < I->getNumOperands();i++)
   {
      I->setOperand(i, UndefValue::get(I->getOperand(i)->getType()));
   }
   //Delete LoadInst
   I->removeFromParent();

   //Store the metadata of I to CI
   SmallVector<std::pair<unsigned int, MDNode*>, 8> MDNodes;
   I->getAllMetadata(MDNodes);
   for(unsigned I = 0; I<MDNodes.size(); ++I){
      CI->setMetadata(MDNodes[I].first, MDNodes[I].second);
   }
   return CI;
}


bool Lock::runOnModule(Module &M)
{
   return false;
}

//runOnModule()
//Unlock the locked Instruction
bool Unlock::runOnModule(Module &M)
{
   for(Module::iterator F = M.begin(), FE = M.end(); F!=FE; ++F){
      inst_iterator I = inst_begin(F);
      while(I!=inst_end(F)){
         Instruction* self = &*I;
         // step first, to void memory crash
         I++;
         //DEBUG(errs()<<"**************************"<<*self<<"\n");
         if(isa<CallInst>(self)){
            unlock_inst(self);
         }
      }
   }
   // remove empty function declare
   Module::iterator F = M.begin();
   while(F!=M.end())
   {
      Function* Ftmp = &*F;
      F++;
      if(Ftmp->getName().find("lock.")==0)
      {
         //DEBUG(errs()<<"*********Remove Function: "<<*Ftmp<<"\n");
         Ftmp->removeFromParent();
      }
   }
   //DEBUG(errs()<<"Remove Function Over!!!\n");
   return false;
}

//unlock_inst()
// Unlock the given locked instruction.
// that is changing the CallInst instruction to the original instruction
void Unlock::unlock_inst(Instruction* I)
{
   DEBUG(errs()<<"\n\n\nthis ins start!!!\n");
   DEBUG(errs()<<"current inst: "<<*I<<"\n");
   LLVMContext& C = I->getContext();
   CallInst* CI=cast<CallInst>(I);
   SmallVector<Type*, 8>OpTypes;
   SmallVector<Value*, 8>OpArgs;
   for(Instruction::op_iterator Op = I->op_begin(), E = I->op_end(); Op!=E; ++Op){
      OpTypes.push_back(Op->get()->getType());
      OpArgs.push_back(Op->get());
   }
   Function* F=CI->getCalledFunction();
   //DEBUG(errs()<<"Function: "<<*F<<"\n");
   std::string cname="";
   if(F!=NULL)
      cname = F->getName().str();
   //Get the metadata of CallInst
   SmallVector<std::pair<unsigned int, MDNode*>, 8> MDNodes;
   CI->getAllMetadata(MDNodes);

   //Get the names of all metadata in the Module
   SmallVector<StringRef, 30> names; 
   C.getMDKindNames(names);

   for(unsigned i = 0;i < OpArgs.size();i++)
      DEBUG(errs()<<"OpArgs"<<i<<": "<<*OpArgs[i]<<"\n");

   //Unlock the LoadInst
   if(cname.find("lock.load") == 0){
      LoadInst* LI=new LoadInst(OpArgs[0],"",I);
      for(unsigned i = 0; i < MDNodes.size(); i++){
         SmallVector<StringRef, 10> tmp;
         //DEBUG(errs()<<names[MDNodes[i].first].str()<<"\n");
         names[MDNodes[i].first].split(tmp,".");
         //cerr<<tmp[0]<<"\t"<<endl;
         if(tmp[0].str()=="volatile")
            LI->setVolatile(true);
         else if(tmp[0].str()=="atomic"){
            LI->setAtomic((AtomicOrdering)(atoi(tmp[1].str().c_str())), (SynchronizationScope)(atoi(tmp[2].str().c_str())));
         }
         else if(tmp[0].str()=="align")
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
      //cerr<<endl;
      DEBUG(errs()<<"found lock.\t"<<*LI<<"\n");
   }
   
   //unlock the store
   else if(cname.find("lock.store") == 0){
      StoreInst* SI = new StoreInst(OpArgs[0],OpArgs[1], I);
      for(unsigned i = 0;i < MDNodes.size(); i++){
         SmallVector<StringRef, 10> tmp;
         //DEBUG(errs()<<names[MDNodes[i].first].str()<<"\n");

         names[MDNodes[i].first].split(tmp,".");
         if(tmp[0].str() == "volatile")
            SI->setVolatile(true);
         else if(tmp[0].str() == "atomic")
            SI->setAtomic((AtomicOrdering)(atoi(tmp[1].str().c_str())), (SynchronizationScope)(atoi(tmp[2].str().c_str())));
         else if(tmp[0].str() == "align")
            SI->setAlignment(atoi(tmp[1].str().c_str()));
         else
            SI->setMetadata(MDNodes[i].first, MDNodes[i].second);
      }
      I->replaceAllUsesWith(SI);
      for(unsigned i = 0; i < I->getNumOperands(); i++){
         I->setOperand(i, UndefValue::get(I->getOperand(i)->getType()));
      }
      I->removeFromParent();
      DEBUG(errs()<<"found lock.\t"<<*SI<<"\n");
   }

   //Unlock the cmp
   else if(cname.find("lock.cmp")==0){
      SmallVector<StringRef, 10> OpandPre;
      StringRef(cname).split(OpandPre, ".");
      CmpInst* CMI = CmpInst::Create((Instruction::OtherOps)(atoi(OpandPre[2].str().c_str())), atoi(OpandPre[3].str().c_str()), OpArgs[0], OpArgs[1], "", I);

      for(unsigned i = 0;i < MDNodes.size(); i++){
         SmallVector<StringRef, 10>tmp; 
         //DEBUG(errs()<<names[MDNodes[i].first].str()<<"\n");
         CMI->setMetadata(MDNodes[i].first, MDNodes[i].second);
      }
      I->replaceAllUsesWith(CMI);
      for(unsigned i = 0;i < I->getNumOperands(); i++){
         I->setOperand(i, UndefValue::get(I->getOperand(i)->getType()));
      }
      I->removeFromParent();
      DEBUG(errs()<<"found lock.\t"<<*CMI<<"\n");

   }

   //Unlock the BinaryOperator
   //like add,mul,sub,etc
   else if(cname.find("lock.BinaryOp")==0){
      SmallVector<StringRef, 10> Opcode;
      StringRef(cname).split(Opcode,".");
      BinaryOperator* BI = BinaryOperator::Create((Instruction::BinaryOps)(atoi(Opcode[3].str().c_str())),OpArgs[0],OpArgs[1],"",I);
      for(unsigned i = 0;i < MDNodes.size(); i++){
         //DEBUG(errs()<<names[MDNodes[i].first].str()<<"\n");

         if(names[MDNodes[i].first].str() == "nsw")
            BI->setHasNoSignedWrap();
         if(names[MDNodes[i].first].str() == "nuw")
            BI->setHasNoUnsignedWrap();
         if(names[MDNodes[i].first].str() == "exact")
            BI->setIsExact();
      }
      I->replaceAllUsesWith(BI);
      for(unsigned i = 0;i < I->getNumOperands(); i++){
         I->setOperand(i, UndefValue::get(I->getOperand(i)->getType()));
      }
      I->removeFromParent();
      DEBUG(errs()<<"found lock.\t"<<*BI<<"\n");
   }
   //Unlock the CastInst
   //like bitcast sextinst...
   else if(cname.find("lock.CastInst")==0){
      SmallVector<StringRef, 10> Opcode;
      StringRef(cname).split(Opcode, ".");
      ///DEBUG(errs()<<OpArgs.size()<<"\n");
      //DEBUG(errs()<<*OpArgs[0]<<"\t "<<*OpArgs[1]<<"\n");
      CastInst* Cast = CastInst::Create((Instruction::CastOps)(atoi(Opcode[3].str().c_str())), OpArgs[0],I->getType(),"",I);
      I->replaceAllUsesWith(Cast);
      for(unsigned i = 0;i < I->getNumOperands();i++)
      {
         I->setOperand(i, UndefValue::get(I->getOperand(i)->getType()));
      }
      I->removeFromParent();
      DEBUG(errs()<<"found lock.\t"<<*Cast<<"\n");
   }
   //Unlock the GetelementPtrInst
   else if(cname.find("lock.getelementptrinst")==0)
   {
      SmallVector<StringRef, 10> Opcode;
      StringRef(cname).split(Opcode, ".");
      Value* Ptr=OpArgs[0];
      //DEBUG(errs()<<OpArgs.size()<<"\n");
      OpArgs.erase(OpArgs.begin());
      OpArgs.pop_back();//the last arg is declare the function lock.getelementptrinst...,we should delete it.
      //DEBUG(errs()<<OpArgs.size()<<"\n");
      ArrayRef<Value*> IdxList(OpArgs); 
      GetElementPtrInst* GEP = GetElementPtrInst::Create(Ptr,IdxList,"" ,I);
      if(Opcode[2].str() == "inbounds")
         GEP->setIsInBounds();
      I->replaceAllUsesWith(GEP);
      for(unsigned i = 0;i < I->getNumOperands();i++)
      {
         I->setOperand(i, UndefValue::get(I->getOperand(i)->getType()));
      }
      I->removeFromParent();
      DEBUG(errs()<<"found lock.\t"<<*GEP<<"\n");

   }
   else
      DEBUG(errs()<<"not found lock.\n");
   DEBUG(errs()<<"this inst is over!\n");
}

#ifdef ENABLE_DEBUG
class LockAll: public ModulePass
{
   public:
   static char ID;
   LockAll():ModulePass(ID) {}
   void getAnalysisUsage(llvm::AnalysisUsage& AU) const
   {
	    AU.setPreservesAll();
       AU.addRequired<Lock>();
	}
   bool runOnModule(llvm::Module& M)
   {
      Lock& L = getAnalysis<Lock>();

      //Iterate through all the instructions of the module
      for(Module::iterator F = M.begin(), FE = M.end(); F!=FE; ++F){
         inst_iterator I = inst_begin(F);

         while(I!=inst_end(F)){
            Instruction* self = &*I;
            I++;
            if(isa<LoadInst>(self))
               L.lock_inst(self);
            if(isa<StoreInst>(self))
               L.lock_inst(self);
            if(isa<CmpInst>(self))
               L.lock_inst(self);
            if(isa<BinaryOperator>(self))
               L.lock_inst(self);
            if(isa<CastInst>(self))
               L.lock_inst(self);
            if(isa<GetElementPtrInst>(self))
               L.lock_inst(self);
         }
      }
      return true;
   }
};
char LockAll::ID = 0;
static RegisterPass<LockAll> Z("LockAll","A test pass to lock all insturctions");
#endif
