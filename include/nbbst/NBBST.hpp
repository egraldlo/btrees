#ifndef NBBST_TREE
#define NBBST_TREE

#include <cassert>

#include "hash.hpp"
#include "Utils.hpp"

namespace nbbst {
    
enum UpdateState {
    CLEAN = 0,
    DFLAG = 1,
    IFLAG = 2,
    MARK  = 3
};

struct Node;

struct Info;
typedef Info* Update;

struct Info {
    Node* gp;               //Internal
    Node* p;                //Internal
    Node* newInternal;      //Internal
    Node* l;                //Leaf
    Update pupdate;
};

typedef Info* Update; 

inline UpdateState getState(Update update){
   return static_cast<UpdateState>(reinterpret_cast<unsigned long>(update) & 3l);
}

inline Update Unmark(Update info){
    return reinterpret_cast<Update>(reinterpret_cast<unsigned long>(info) & (~0l - 3));
}

inline Update Mark(Update info, UpdateState state){
    return reinterpret_cast<Update>((reinterpret_cast<unsigned long>(info) & (~0l - 3)) | static_cast<unsigned int>(state));
}

struct Node {
    bool internal;
    int key;
    
    Update update;
    Node* left;
    Node* right;
};

struct SearchResult {
    Node* gp;       //Internal
    Node* p;        //Internal
    Node* l;        //Leaf
    Update pupdate;
    Update gpupdate;

    SearchResult() : gp(nullptr), p(nullptr), l(nullptr), pupdate(nullptr), gpupdate(nullptr) {}
};

template<typename T, int Threads>
class NBBST {
    public:
        NBBST();
        ~NBBST();

        bool contains(T value);
        bool add(T value);
        bool remove(T value);

    private:
        void Search(int key, SearchResult* result);      
        void HelpInsert(Info* op);
        bool HelpDelete(Info* op);
        void HelpMarked(Info* op);
        void Help(Update u);
        void CASChild(Node* parent, Node* old, Node* newNode);

        /* Allocate stuff from the hazard manager  */
        Node* newInternal(int key);
        Node* newLeaf(int key);
        Info* newIInfo(Node* p, Node* newInternal, Node* l);
        Info* newDInfo(Node* gp, Node* p, Node* l, Update pupdate);
        
        /* To remove properly a node  */
        void releaseNode(Node* node);

        Node* root;

        HazardManager<Node, Threads, 3> nodes;
        HazardManager<Info, Threads, 3> infos;
};

template<typename T, int Threads>
NBBST<T, Threads>::NBBST(){
    root = newInternal(std::numeric_limits<int>::max());
    root->update = Mark(nullptr, CLEAN);

    root->left = newLeaf(std::numeric_limits<int>::min());
    root->right = newLeaf(std::numeric_limits<int>::max());
}

template<typename T, int Threads>
NBBST<T, Threads>::~NBBST(){
    //Remove the three nodes created in the constructor
    releaseNode(root->left);
    releaseNode(root->right);
    releaseNode(root);
}

template<typename T, int Threads>
Node* NBBST<T, Threads>::newInternal(int key){
    Node* node = nodes.getFreeNode();

    node->update = nullptr;
    node->left = nullptr;
    node->right = nullptr;

    node->internal = true;
    node->key = key;

    return node;
}

template<typename T, int Threads>
Node* NBBST<T, Threads>::newLeaf(int key){
    Node* node = nodes.getFreeNode();
    
    node->update = nullptr;
    node->left = nullptr;
    node->right = nullptr;

    node->internal = false;
    node->key = key;

    return node;
}
        
template<typename T, int Threads>
Info* NBBST<T, Threads>::newIInfo(Node* p, Node* newInternal, Node* l){
    Info* info = infos.getFreeNode();

    info->gp = nullptr;
    
    info->pupdate = nullptr;
    info->p = p;
    info->newInternal = newInternal;
    info->l = l;

    return info;
}

template<typename T, int Threads>
Info* NBBST<T, Threads>::newDInfo(Node* gp, Node* p, Node* l, Update pupdate){
    Info* info = infos.getFreeNode();

    info->newInternal = nullptr;
    
    info->gp = gp;
    info->p = p;
    info->l = l;
    info->pupdate = pupdate;

    return info;
}

template<typename T, int Threads>
void NBBST<T, Threads>::releaseNode(Node* node){
    if(node){
        if(node->update){
            infos.releaseNode(Unmark(node->update));
        }

        nodes.releaseNode(node);
    }
}

template<typename T, int Threads>
void NBBST<T, Threads>::Search(int key, SearchResult* result){
    Node* l = root;

    while(l->internal){
        result->gp = result->p;
        result->p = l;
        result->gpupdate = result->pupdate;
        result->pupdate = result->p->update;

        if(key < l->key){
            l = result->p->left;
        } else {
            l = result->p->right;
        }
    }

    result->l = l;
}

template<typename T, int Threads>
bool NBBST<T, Threads>::contains(T value){
    int key = hash(value);

    SearchResult result;
    Search(key, &result);

    return result.l->key == key;
}

template<typename T, int Threads>
bool NBBST<T, Threads>::add(T value){
    int key = hash(value);

    Node* newNode = newLeaf(key);

    SearchResult search;

    while(true){
        Search(key, &search);

        nodes.publish(search.l, 0);
            
        infos.publish(search.p->update, 0);
        infos.publish(search.pupdate, 1);

        if(search.l->key == key){
            nodes.releaseAll();
            nodes.releaseNode(newNode);
            
            infos.releaseAll();

            return false; //Key already in the set
        }

        if(getState(search.pupdate) != CLEAN){
            Help(search.pupdate);
        } else {
            Node* newSibling = newLeaf(search.l->key);
            Node* newInt = newInternal(std::max(key, search.l->key));
            newInt->update = Mark(nullptr, CLEAN);
            
            //Put the smaller child on the left
            if(newNode->key <= newSibling->key){
                newInt->left = newNode;
                newInt->right = newSibling;
            } else {
                newInt->left = newSibling;
                newInt->right = newNode;
            }

            Info* op = newIInfo(search.p, newInt, search.l);
            infos.publish(op, 2);

            Update result = search.p->update;
            if(CASPTR(&search.p->update, search.pupdate, Mark(op, IFLAG))){
                nodes.releaseAll();
                infos.releaseAll();

                HelpInsert(op);

                if(result){
                    infos.releaseNode(Unmark(result));
                }

                return true;
            } else {
                nodes.releaseAll();
                nodes.releaseNode(newInt);
                nodes.releaseNode(newSibling);
                
                infos.releaseAll();
                infos.releaseNode(op);

                Help(result);
            }
        }
    }
}

template<typename T, int Threads>
bool NBBST<T, Threads>::remove(T value){
    int key = hash(value);

    SearchResult search;

    while(true){
        Search(key, &search);
        nodes.publish(search.l, 0);
        nodes.publish(search.p, 1);
        nodes.publish(search.l, 2);
        
        infos.publish(search.gpupdate, 0);
        infos.publish(search.pupdate, 1);
        
        if(search.l->key != key){
            infos.releaseAll();
            nodes.releaseAll();
            return false;
        }

        if(getState(search.gpupdate) != CLEAN){
            Help(search.gpupdate);
            infos.releaseAll();
            nodes.releaseAll();
        } else if(getState(search.pupdate) != CLEAN){
            Help(search.pupdate);
            infos.releaseAll();
            nodes.releaseAll();
        } else {
            //Perhaps this test is not necessary
            if(!search.gp){
                infos.releaseAll();
                nodes.releaseAll();
                
                //Retry
                continue;
            }

            infos.publish(search.gp->update, 0);
            
            Info* op = newDInfo(search.gp, search.p, search.l, search.pupdate);

            Update result = search.gp->update;
            if(CASPTR(&search.gp->update, search.gpupdate, Mark(op, DFLAG))){
                infos.releaseNode(Unmark(result));

                if(HelpDelete(op)){
                    infos.releaseAll();
                    nodes.releaseAll();
                    
                    return true;
                }
            } else {
                infos.releaseNode(op);

                Help(result);
            }
            
            infos.releaseAll();
            nodes.releaseAll();
        }
    }
}

template<typename T, int Threads>
void NBBST<T, Threads>::Help(Update u){
    if(getState(u) == IFLAG){
        HelpInsert(Unmark(u));
    } else if(getState(u) == MARK){
        HelpMarked(Unmark(u));
    } else if(getState(u) == DFLAG){
        HelpDelete(Unmark(u));
    }
}

template<typename T, int Threads>
void NBBST<T, Threads>::HelpInsert(Info* op){
    infos.publish(op, 0);
    infos.publish(op->p->update, 1);

    CASChild(op->p, op->l, op->newInternal);
    CASPTR(&op->p->update, Mark(op, IFLAG), Mark(op, CLEAN));

    infos.releaseAll();
}

template<typename T, int Threads>
bool NBBST<T, Threads>::HelpDelete(Info* op){
    infos.publish(op->p->update, 0);
    infos.publish(op->pupdate, 1);
    infos.publish(op, 2);

    Update result = op->p->update;

    //If we succeed
    if(CASPTR(&op->p->update, op->pupdate, Mark(op, MARK))){
        if(result){
            infos.releaseNode(Unmark(result));
        }
        //nodes.releaseNode(op->l);
        HelpMarked(Unmark(op));
        infos.releaseAll();
        return true;
    } 
    //if another has succeeded for us
    else if(getState(op->p->update) == MARK && Unmark(op->p->update) == Unmark(op)){
        HelpMarked(Unmark(op));
        infos.releaseAll();
        return true;
    } else {
        Help(result);

        infos.publish(op->gp->update, 0);
        infos.publish(op, 1);
        CASPTR(&op->gp->update, Mark(op, DFLAG), Mark(op, CLEAN));
        infos.releaseAll();

        return false;
    }
}

template<typename T, int Threads>
void NBBST<T, Threads>::HelpMarked(Info* op){
    Node* other;

    if(op->p->right == op->l){
        other = op->p->left;
    } else {
        other = op->p->right;
    }

    CASChild(op->gp, op->p, other);

    infos.publish(op->gp->update, 0);
    infos.publish(op, 1);
    CASPTR(&op->gp->update, Mark(op, DFLAG), Mark(op, CLEAN));
    infos.releaseAll();
}
        
template<typename T, int Threads>
void NBBST<T, Threads>::CASChild(Node* parent, Node* old, Node* newNode){
    nodes.publish(old, 0);
    nodes.publish(newNode, 1);

    if(newNode->key < parent->key){
        nodes.publish(parent->left, 2);
        if(CASPTR(&parent->left, old, newNode)){
            if(old){
                nodes.releaseNode(old);
            }
        }
    } else {
        nodes.publish(parent->right, 2);
        if(CASPTR(&parent->right, old, newNode)){
            if(old){
                nodes.releaseNode(old);
            }
        }
    }

    nodes.releaseAll();
}

} //end of nbbst

#endif
