//--- Developed by Jing Yu ----//
//  ShortcutDetector.cpp       //
//================================================//
//This file implements a shortcut detector.
//Specifically, this
//        *Detect very simple short cut branches 
//        *Be able to distinguish chained conditional expressions from others
//The next step is 
//        //*Insert counters to profile execution frequency of these short cut branches
//================================================//
#define Jing_DEBUG 1

#define DEBUG_TYPE "shortcut"
#define MAX(a,b) (((a)>(b))?(a):(b))

#include "ShortcutDetector.h"
#include <llvm/ADT/Statistic.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Analysis/Dominators.h>

using namespace llvm;

////////////////////////////////
//struct DominatorSet; 
STATISTIC(NumShortcut, "Number of shortcut branches detected");
STATISTIC(NumShortcutSet, "Number of shortcut branche SETs detected");

char ShortcutDetectorPass::ID = 0;

namespace {
   RegisterPass<ShortcutDetectorPass> X("ShortcutDT", "Detect Shortcut", false, true);
}

/////////////////////////////////////////
////ShortcutDetectorPass CLASS///////////
/////////////////////////////////////////
//Public interface to create the ShortcutDetector pass.
//FunctionPass *llvm::createShortcutDetectorPass() {return new ShortcutDetectorPass(); }


void ShortcutDetectorPass::getAnalysisUsage(AnalysisUsage &AU) const{
   AU.addRequired<DominatorTree>();
   AU.setPreservesAll();
}

/*Check if BB is two-way conditional branch */
bool ShortcutDetectorPass::isTwowayBranch (BasicBlock *BB) {
   if (BranchInst *BI = dyn_cast<BranchInst>(BB->getTerminator())) {
      if (BI ->isConditional()) return true;
   }
   return false;
}

/*Check if BB is only a branch node *
 *  - Every target has no use in other block*
 *  - Every target does not appear in earlier instructions in this block *
 *  - There are no store */
bool ShortcutDetectorPass::isOnlyBranch (BasicBlock *BB) {
   bool onlybranch = true;
   std::set<Instruction *> InstMark;
   for (BasicBlock::iterator Inst = BB->begin(), E = BB->end(); Inst!=E; ++Inst) {
      Instruction *I = Inst;
      InstMark.insert(I);
      /*do not write to memory*/
      if (I->mayWriteToMemory()) {onlybranch = false; break;}

      /*iterate over all uses of I*/
      for (Value::use_iterator Inst_use = I->use_begin(), use_end=I->use_end(); Inst_use!=use_end; Inst_use++) {
         /*do not use by other blocks */
         Instruction *use_I = dyn_cast<Instruction>(*Inst_use);
         BasicBlock *use_block = use_I->getParent();
         if (use_block!=BB) {onlybranch = false; break;}

         /*the use_I is not before I in current basicblock*/
         if (InstMark.count(use_I)!=0) { 
            onlybranch=false; 
            break;
         }
      }
      if (!onlybranch) break;
   }

#ifdef Jing_DEBUG
   /*debug - DEBUG() can only be invoked in debug build*/
   //#undef DEBUG_TYPE
   //#define DEBUG_TYPE "onlybranch"
   //std::cerr << "Check isOnlyBranch for block " << BB->getName() << ":" << onlybranch << "\n";
   //#undef DEBUG_TYPE
#endif

   return onlybranch;
}



/*Check if BB jumps back to its ancestors*/

bool ShortcutDetectorPass::isJumpBack (BasicBlock *BB, BasicBlock *Target ) {
   /*We use dominance graph to do this. Suppose the edge from BB to Target is backward, Target must be the dominator of BB, and vice versa. So we only need to check if Target dominates BB */
   DominatorTree& DT = getAnalysis<DominatorTree>();
   assert(DT.isReachableFromEntry(BB) && "BB should be reachable!");
   return DT.dominates(Target, BB);
}



/*dump all short-cuts we found*
 *     BB1      *
 *    /  \      *
 *   /   BB2    *
 *  /   /  \    *
 *  BB3     BB4 */
void ShortcutDetectorPass::dumpShortcut (std::list<ChildrenSet*> &headlist) {
   std::list<ChildrenSet*>::iterator nodesetI, nodesetE;
   for (nodesetI = headlist.begin(), nodesetE = headlist.end() ; nodesetI!=nodesetE; nodesetI++) {
      ChildrenSet *thisnode = *nodesetI;
      //assert (SCSetMap.count(thisnode)>0 && "ERROR: thisnode should have an entry on SCSetMap!"); - old
      //ChildrenSet *CSforThis = (*(SCSetMap.find(thisnode))).second; -old
      assert (thisnode->isHead() && "Error:headlist should only contain head"); 
      thisnode->dump(); 
      NumShortcut += thisnode->getSCnum();
      localshortcut += thisnode->getSCnum();  
      localSCset ++;
      NumShortcutSet ++;

   }

   std::cerr << "local shortcut number: " << localshortcut <<"\n";
   std::cerr << "local shortcut sets (nested if): " << localSCset << "\n";
   std::cerr << "local sets that failed domination Verify:"<<localFailed <<"\n\n\n";
}


/*hasBackEdge() - only deals with BranchInst
  return true for all other terminators  */
bool ShortcutDetectorPass::hasBackEdge(BasicBlock *BB) {
   bool hasbackedge = true;
   if (BranchInst *BI = dyn_cast<BranchInst>(BB->getTerminator())) { 
      BasicBlock *Target1 = BI->getSuccessor(0);
      hasbackedge = isJumpBack(BB,Target1);
      if (BI->isConditional()) 
         hasbackedge = hasbackedge || isJumpBack(BB, BI->getSuccessor(1));
   }
   return hasbackedge;
}


/** runOnFunction*/
bool ShortcutDetectorPass::runOnFunction(Function &F) {
   DominatorTree& DT = getAnalysis<DominatorTree>();

   /*print the information for this function*/
   localshortcut=0;
   localSCset = 0;
   localFailed = 0;

   errs() << "**********func: " << F.getName() << " ********\n";

   /*leafset: The set of basic blocks that satisfy any one of the following conditions  
     - not two-way branch
     - out-degree has a backward edge
     - not reachable
     - has non-branch contents
nodeset: the set of basic blocks that satisfy all of the followings
- two-way branch
- no backward edge
- reachable
    ***/
   std::set<BasicBlock*> leafset, nodeset;


   /*Scan all basic blocks in this function and classify them into leafset, nodeset and canOnlytopset*/
   for (Function::iterator iBB = F.begin(), E=F.end(); iBB != E; ++iBB) {
      BasicBlock *BB = iBB;
      if (!(DT.isReachableFromEntry(BB)) || !(isTwowayBranch(BB)) || (hasBackEdge(BB)) ) { 
         leafset.insert(BB);
      } else {
         nodeset.insert(BB);
         if (!isOnlyBranch(BB)) leafset.insert(BB);
      }
   }

   //debug - let's see what are contents in leafset and nodeset
#ifdef Jing_DEBUG
   errs() << "DEBUG::: let's dump leafset...\n";
   std::set<BasicBlock*>::iterator setI;
   for (setI = leafset.begin(); setI != leafset.end(); ++setI) {
      errs() << (*setI)->getName() << "  ";
   }
   errs() << "\nDEBUG:: let's dump nodeset...\n";
   for (setI = nodeset.begin(); setI != nodeset.end(); ++setI) {
      errs() << (*setI)->getName() << "  ";
   }
   errs() << "\n";
#endif
   //end of debug

   std::map<BasicBlock*, ChildrenSet*> SCSetMap;

   //call SCSetMap constructor
   conSCSetMap(leafset,nodeset, SCSetMap);

   //build HeadNodeList. Verify domination attribute
   BuildHeadNodeList (SCSetMap, F);

   //clear useless nodes in SCSetMap
   ClearUselessNodesin (SCSetMap,HeadNodeList);

   //construct EdgeGraph
   conEdgeGraph(HeadNodeList);

   /*all right, now we have all shortcut information  *
     We can process it and output*/
   dumpShortcut (HeadNodeList);


   return false;
}

//clear useless nodes in SCSetMap
void 
ShortcutDetectorPass::ClearUselessNodesin (std::map<BasicBlock*,ChildrenSet*>&SCSetMap, std::list<ChildrenSet*>&HeadNodeList) {
   //to do
} 


//build HeadNodeList. Verify domination attribute
   void
ShortcutDetectorPass::BuildHeadNodeList(std::map<BasicBlock*,ChildrenSet*>&SCSetMap, Function &F) 
{
   for (Function::iterator iBB = F.begin(), E=F.end(); iBB != E; ++iBB) {
      BasicBlock *BB = iBB;
      if (SCSetMap.count(BB)>0) {
         ChildrenSet *children =  (*(SCSetMap.find(BB))).second;
         if (children->isHead()) {
            //verify this the head can dominate all its SCmidnodes
            if (verify_domination(children)) 
               HeadNodeList.push_back(children);
            else localFailed++;
         }
      }
   }
}


bool
ShortcutDetectorPass::verify_domination(ChildrenSet* sethead) {
   return (sethead->verify_domination(getAnalysis<DominatorTree>()));
}



///////////////////////////////////
///construct ChildrenSet Map   ////
///////////////////////////////////
void 
ShortcutDetectorPass::conSCSetMap(std::set<BasicBlock*> &leafset, std::set<BasicBlock*> &nodeset, std::map<BasicBlock*,ChildrenSet*> &SCSetMap) {

   ChildrenSet *newChildrenSet;

   /*Iterate over nodeset and seek for shortcut edges */
   bool changed;
   std::set<BasicBlock*>::iterator nodesetI, nodesetE;

   do {
      changed = false;

      for (nodesetI = nodeset.begin(), nodesetE = nodeset.end() ; nodesetI!=nodesetE; nodesetI++) {
         BasicBlock * thisNode = *nodesetI;
         if (SCSetMap.count(thisNode)==0) {
            //bool canOnlytop = leafset.count(thisNode)>0;
            BranchInst *thisNodeBranchI = dyn_cast<BranchInst>(thisNode->getTerminator());
            BasicBlock *leftChild = thisNodeBranchI ->getSuccessor(0);
            BasicBlock *rightChild = thisNodeBranchI -> getSuccessor(1);

            if ( (leafset.count(leftChild)) > 0 ) {
               if ((leafset.count(rightChild)) > 0) {
                  /*Two children are leaves */
                  newChildrenSet = new ChildrenSet(thisNode,leftChild,rightChild);
                  SCSetMap.insert(std::pair<BasicBlock*,ChildrenSet*>(thisNode,newChildrenSet));
                  changed = true;
               } else { 
                  if ((SCSetMap.count(rightChild)) > 0) {
                     /*one leaf, one intermediate node*/
                     newChildrenSet = new ChildrenSet(thisNode,leftChild,(*(SCSetMap.find(rightChild))).second);
                     SCSetMap.insert(std::pair<BasicBlock*,ChildrenSet*>(thisNode, newChildrenSet));
                     changed = true;
                  }
               }  
            }  else if (SCSetMap.count(leftChild)>0) {
               if (leafset.count(rightChild)>0) {
                  /*one leaf, one intermediate node*/
                  newChildrenSet = new ChildrenSet(thisNode,(*(SCSetMap.find(leftChild))).second, rightChild);
                  SCSetMap.insert(std::pair<BasicBlock*,ChildrenSet*>(thisNode, newChildrenSet));
                  changed=true;
               } else {
                  if (SCSetMap.count(rightChild)>0) {
                     /*two are intermediate nodes*/
                     newChildrenSet = new ChildrenSet(thisNode,(*(SCSetMap.find(leftChild))).second,(*(SCSetMap.find(rightChild))).second );
                     SCSetMap.insert(std::pair<BasicBlock*,ChildrenSet*>(thisNode, newChildrenSet));
                     changed = true;
                  }
               }
            } 
         }  //if thisNode does not have an entry on SCSetMap  
      } //end of for 
   } while (changed);

}


//////////////////////////
//construct EdgeGraph   //
//////////////////////////
void 
ShortcutDetectorPass::conEdgeGraph(std::list<ChildrenSet*> &HeadNodeList) {
   std::list<ChildrenSet*>::iterator iter;
   ChildrenSet *SCHead;
   for (iter=HeadNodeList.begin(); iter!=HeadNodeList.end(); iter++) {
      SCHead=*iter;
      std::set<ChildrenSet*> *midnodeset = SCHead->getSCmidnodeset();
      std::list<ChildrenSet*>WorkList;
      std::set<ChildrenSet*> Marked;
      WorkList.push_front(SCHead);

      while (!WorkList.empty()) {
         ChildrenSet* curNode = WorkList.front();
         WorkList.pop_front();
         if (Marked.count(curNode)==0) {
            curNode->conEdgeGraph(midnodeset);
            Marked.insert(curNode);
            if (ChildrenSet *leftchild = curNode->leftchildrenset) 
               if (midnodeset->count(leftchild)>0) 
                  WorkList.push_back(leftchild);
            if (ChildrenSet *rightchild = curNode->rightchildrenset)
               if (midnodeset->count(rightchild)>0)
                  WorkList.push_back(rightchild);
         }
      }
   }

}


////////////////////////////////////////////////
////ChildrenSet CLASS //////////////////////////
////////////////////////////////////////////////
void 
ChildrenSet::init(BasicBlock *thisBB, int leftlevel, int rightlevel) {
   myBB = thisBB;
   level = MAX(leftlevel,rightlevel) + 1;
   head = haveSC =isleftSC=isrightSC = false;
   uplink = NULL; 
   nummidnodes = 0;
   SCmidnodeset = NULL;
   rightchildrenset=NULL;
   leftchildrenset =NULL;
   rightchildBB = NULL;
   leftchildBB = NULL;
   SCnum = 0;
   out0 = out1 = NULL;
   inEdges = NULL;
}

ChildrenSet::ChildrenSet (BasicBlock *thisBB, BasicBlock *leftleaf, BasicBlock *rightleaf) {
   init(thisBB, 0,0);
   leftchildBB = leftleaf;
   rightchildBB = rightleaf;
   //setUnion (leftleaf, rightleaf);
}

ChildrenSet::ChildrenSet (BasicBlock *thisBB, BasicBlock *leftleaf, ChildrenSet *rightSet) {
   init(thisBB, 0, rightSet->getLevel());
   leftchildBB = leftleaf;
   rightchildrenset = rightSet;
   //setUnion(rightSet, leftleaf);
   int leftLast = 0;
   if (ChildrenSet* findMidnode = isShortcut(leftleaf, rightSet, &leftLast)) {
      head = true;
      haveSC = true;
      isleftSC = true;
      //rightSet->invalidateHead();
      //SCnum = rightSet->getSCnum() + 1;
      mySCpath = getmySCpath(findMidnode,rightSet, leftLast, &nummidnodes);
      SCmidnodeset = getallmidnodeset(findMidnode, rightSet,&SCnum);
   }
} 

ChildrenSet::ChildrenSet (BasicBlock *thisBB, ChildrenSet *leftSet, BasicBlock *rightleaf) {
   init(thisBB, leftSet->getLevel(), 0);
   leftchildrenset = leftSet;
   rightchildBB = rightleaf;
   //setUnion(leftSet, rightleaf);
   int leftLast = 0;
   if (ChildrenSet *findMidnode = isShortcut(rightleaf, leftSet, &leftLast)) {
      head = true;
      haveSC = true;
      isrightSC = true;
      //leftSet->invalidateHead();
      //SCnum = leftSet->getSCnum() + 1;
      mySCpath = getmySCpath(findMidnode,leftSet,leftLast,&nummidnodes);
      SCmidnodeset = getallmidnodeset(findMidnode, leftSet,&SCnum);

   }
}

ChildrenSet::ChildrenSet (BasicBlock *thisBB, ChildrenSet *leftSet, ChildrenSet *rightSet) {
   init(thisBB, leftSet->getLevel(), rightSet->getLevel());
   leftchildrenset = leftSet;
   rightchildrenset = rightSet;
   //setUnion(leftSet, rightSet);
   int leftLast = 0;
   if (ChildrenSet *findMidnode = isShortcut(leftSet->getBB(), rightSet, &leftLast)) {
      head = true;
      haveSC = true;
      isleftSC = true;
      //rightSet->invalidateHead();
      //SCnum = leftSet->getSCnum() + rightSet->getSCnum() + 1;
      mySCpath = getmySCpath(findMidnode,rightSet,leftLast,&nummidnodes);
      SCmidnodeset = getallmidnodeset(findMidnode, rightSet,&SCnum);
   }
   leftLast = 0;
   if (ChildrenSet *findMidnode = isShortcut(rightSet->getBB(), leftSet,&leftLast)) {
      assert (!haveSC && "ERROR, one BB could not have two shortcuts on both children!");
      head = true;
      haveSC = true;
      isrightSC = true;
      //leftSet->invalidateHead();
      //SCnum = leftSet->getSCnum() + rightSet->getSCnum() + 1;
      mySCpath = getmySCpath(findMidnode,leftSet,leftLast,&nummidnodes);
      SCmidnodeset = getallmidnodeset(findMidnode, leftSet,&SCnum);
   }
}

/********old version
  void
  ChildrenSet::setUnion (ChildrenSet *child1, ChildrenSet *child2) {
  std::set<BasicBlock*> *childset1 = child1->getCHnodes();
  std::set<BasicBlock*> *childset2 = child2->getCHnodes();
  allchnodes.insert(childset1->begin(),childset1->end());
  allchnodes.insert(child1->getBB());
  std::set<BasicBlock*>::iterator iter;
  if (allchnodes.count(child2->getBB())==0) 
  allchnodes.insert(child2->getBB());
  BasicBlock *iterBB;
  for (iter=childset2->begin(); iter != childset2->end(); iter++) {
  iterBB = *iter;
  if (allchnodes.count(iterBB)==0) allchnodes.insert(iterBB);
  } 
  }

  void 
  ChildrenSet::setUnion (ChildrenSet *child1, BasicBlock *child2) {
  std::set<BasicBlock*> *childset1 = child1->getCHnodes();
  allchnodes.insert(childset1->begin(),childset1->end());
  allchnodes.insert(child1->getBB());
  if (allchnodes.count(child2)==0) 
  allchnodes.insert(child2); 
  }

  void
  ChildrenSet::setUnion (BasicBlock *child1, BasicBlock *child2) {
  allchnodes.insert(child1);
  allchnodes.insert(child2);
  }

 *****old version
 */
/**union the two sets and put the result on the first one*/
void
ChildrenSet::ChildrenSetUnion (std::set<ChildrenSet*>* sum, std::set<ChildrenSet*>*operand) {
   ChildrenSet * iterBB;
   std::set<ChildrenSet*>::iterator iter;
   for (iter=operand->begin(); iter != operand->end(); iter++) {
      iterBB = *iter;
      if (sum->count(iterBB)==0) sum->insert(iterBB);
   } 
}

/*insert the second argument into the first set*/
void
ChildrenSet::ChildrenSetInsert(std::set<ChildrenSet*>* sum, ChildrenSet* elem) {
   if (sum->count(elem)==0) sum->insert(elem);
}


/*** check if the tree with root has a node named key*/
ChildrenSet *
ChildrenSet::isShortcut(BasicBlock *key, ChildrenSet *root, int *lastLeft) { 
   /*old version  - find shortcut using all children nodes
     std::set<BasicBlock*> * childset2 =child2->getCHnodes();
     if (childset2->count(child1) > 0) return true;
     else return false;
     */
   ChildrenSet *findMidnode = NULL;
   std::list<ChildrenSet*> WorkList;
   std::set<ChildrenSet*> Marked;
   WorkList.push_front (root);
   Marked.insert(root);

   while (!WorkList.empty()){
      ChildrenSet *curNode = WorkList.front();
      WorkList.pop_front();
      if ( BasicBlock *leftChildBB = curNode->leftchildBB) {
         //left child is a leaf
         if (leftChildBB == key) {
            //find the key
            findMidnode = curNode;
            *lastLeft = 1;
            break;
         }
      }
      if (BasicBlock *rightChildBB = curNode->rightchildBB) {
         //rightchild is a leaf
         if (rightChildBB == key) {
            //find the key
            findMidnode = curNode;
            *lastLeft = -1;
            break;
         }
      }
      if (ChildrenSet *leftChildren = curNode->leftchildrenset) {
         //left child is a node in childrenset tree
         if (Marked.count(leftChildren)==0) {
            //this child was not accessed before
            if (leftChildren->getBB() == key) {
               findMidnode = curNode;
               *lastLeft = 1;
               break;
            } else {
               leftChildren->setUplink (curNode, true);
               WorkList.push_back(leftChildren);
               Marked.insert(leftChildren);
            }
         }
      }
      if (ChildrenSet *rightChildren = curNode->rightchildrenset) {
         //right child is a node in childrenset tree
         if (Marked.count(rightChildren)==0) {
            //this child was not accessed before
            if (rightChildren->getBB() == key) {
               findMidnode = curNode;
               *lastLeft = -1;
               break;
            } else {
               rightChildren->setUplink (curNode, false);
               WorkList.push_back(rightChildren);
               Marked.insert(rightChildren);
            }
         }
      }       
   }//end of while
   return findMidnode;
}


/**collect all midnodeset for this midnode path. The side effect is that all heads were invalidated*/
std::set<ChildrenSet*> *
ChildrenSet::getallmidnodeset(ChildrenSet* findMidnode, ChildrenSet *pathstart, int*totalSCnum) {
   int totalSC = 0;
   std::set<ChildrenSet*> *allmidnodeset = new std::set<ChildrenSet*>();
   while (findMidnode != pathstart) {
      if (findMidnode->isHead()) {
         findMidnode->invalidateHead();
         totalSC += findMidnode->getSCnum();
         ChildrenSetUnion(allmidnodeset, findMidnode->getSCmidnodeset());
      }
      ChildrenSetInsert(allmidnodeset, findMidnode);
      findMidnode=findMidnode->getUplink();
      assert(findMidnode &&"Error:getUplink should not be null!");
   }
   totalSC ++; //add myself
   if (findMidnode->isHead()) {
      findMidnode->invalidateHead();
      ChildrenSetUnion(allmidnodeset, findMidnode->getSCmidnodeset());
      totalSC += findMidnode->getSCnum();
   }
   ChildrenSetInsert(allmidnodeset,findMidnode);
   (*totalSCnum) = totalSC;
   return allmidnodeset;
}


/**return the midnode path*/
std::list<bool> *
ChildrenSet::getmySCpath (ChildrenSet *findMidnode, ChildrenSet*pathstart, int lastLeft, int*mynummidnodes){
   int midnode = 1; //myself
   std::list<bool> *mySCpath = new std::list<bool>();
   //test which side key resides in
   assert(lastLeft!=0 && "Error:lastLeft was not set");
   bool islastLeft = ((lastLeft==1)? true:false); 
   mySCpath->push_front(islastLeft);
   while (findMidnode != pathstart) {
      midnode++;
      mySCpath->push_front(findMidnode->isMomLchild());
      findMidnode=findMidnode->getUplink();
      assert(findMidnode && "Error:getUplink should not be null!");
   }
   (*mynummidnodes) = midnode;
   return mySCpath;
}


   static 
std::string itos(int i) 
{
   std::stringstream s;
   s << i;
   return s.str();
}

   static
std::string listtos(std::list<bool> *path)
{
   std::string *s = new std::string();
   std::list<bool>::iterator pathiter,pathend;
   pathend = path->end();
   for (pathiter=path->begin(); pathiter!=pathend; pathiter++) {
      if (*pathiter) { (*s)+="L";}
      else {(*s)+="R";}
   }
   return *s;
}

std::string 
ChildrenSet::dump(std::string prefix, std::set<ChildrenSet*>*midNodesforthisSet, ChildrenSet* thissetHead) {
   std::string mystring (prefix);
   mystring += "-";
   //information about myself
   mystring += myBB->getName().str() + " L(" + itos(level)+")" ;
   if (isHead()) mystring += " (Head)";
   if (haveSC) mystring += " (haveSC) path(" + listtos(mySCpath) +")";
   if (isleftSC) mystring += " (isleftSC)";
   if (isrightSC) mystring += " (isrightSC)";
   mystring += "\n";

   if ((thissetHead==this) || midNodesforthisSet->count(this) >0) {
      //ok we can dump edge information also
      assert(out0 && "out0 should have already been built");
      assert(out1 && "out1 should have already been build");
      mystring+=out0->dump(prefix)+out1->dump(prefix);
      //information about children
      if (leftchildBB) 
         mystring += prefix+ " |" + leftchildBB->getName().str() + " (leaf)\n"; 
      else 
         mystring += leftchildrenset->dump(prefix+" |", midNodesforthisSet,thissetHead); 

      if (rightchildBB)
         mystring += prefix + "  " +rightchildBB->getName().str() + " (leaf)\n";
      else 
         mystring += rightchildrenset->dump(prefix+"  ",midNodesforthisSet,thissetHead);
   }

   return mystring;
}

void
ChildrenSet::dump() {
   assert(head && "Error:dump() should be only called for head nodes"); 
   std::set<ChildrenSet*> *midNodesforthisSet = SCmidnodeset;
   errs() << "----Dump start from " << myBB->getName() << "------\n";
   errs() << dump(" ", midNodesforthisSet,this) << "\n";
}


bool 
ChildrenSet::verify_domination(DominatorTree& DT){
   assert(head && "Error: verify_domination() should only be called by head");
   std::set<ChildrenSet*>::iterator iter;

   for (iter=SCmidnodeset->begin(); iter!=SCmidnodeset->end(); iter++) {
      BasicBlock *childBB = (*iter)->getBB();
      if (!(DT.dominates(myBB, childBB))) return false;
   }
   return true;
}


//construct outgoing edges for this node
//also add likes of the new edges to their corresponding target nodes if their targets are within *midnodeset*
void ChildrenSet::conEdgeGraph(std::set<ChildrenSet*>*midnodeset) {
   //#ifdef Jing_DEBUG
   //std::cerr<< "Debug:: come to conEdgeGraph for "<< myBB->getName()<<"\n";
   //#endif
   if (leftchildBB) out0 = new Edge(this,leftchildBB); 
   else {
      out0 = new Edge(this, leftchildrenset->getBB());
      if (midnodeset->count(leftchildrenset)>0) {
         //left child is within the scope
         leftchildrenset->addinEdges(out0);
      }
   }
   if (rightchildBB) out1 = new Edge(this,rightchildBB);
   else {
      out1 = new Edge(this, rightchildrenset->getBB());
      if (midnodeset->count(rightchildrenset)>0) {
         //right child is within the scope
         rightchildrenset->addinEdges(out1);
      }
   }
}

void ChildrenSet::addinEdges(Edge *inEdge) {
   if (inEdges == NULL) inEdges = new std::list<Edge*>();
   inEdges->push_back(inEdge);
}


/////////////////////////////////////
///Rep class                     ////
///Edge class                    ////
/////////////////////////////////////

bool 
Rep::notTo(ChildrenSet *target) {return (mynotTo == target->getBB());}

Edge::Edge(ChildrenSet *from, ChildrenSet *to) {
   fromNode = from; toNode = to->getBB();
}


std::string 
Edge::dump(std::string prefix) {
   std::string s(prefix);
   s += "  Edge("+ fromNode->getBB()->getName().str()+"->"+toNode->getName().str()+") ";
   s+="propgtRep:"+dump(propgtRep)+";";
   s+="fixRep:"+dump(fixRep)+"\n";
   return s;
}

// vim: ts=3 sw=3 sts=3 et
