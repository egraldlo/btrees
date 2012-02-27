#include "skiplist/Constants.hpp"

#include "hash.hpp"

namespace skiplist {
    
template<typename T>
class Node {
    public:
        const T value;
        const int key;
        Node<T>** next;//TODO Check if possible to declare directly static array

    private:
        int topLevel;

    public:
        //constructor for sentinel nodes
        Node(int key);

        //constructor for ordinary nodes
        Node(T x, int height);

        ~Node();
};

template<typename T>
Node<T>::Node(int k) : value(k), key(k), topLevel(MAX_LEVEL) {

    next = (Node<T>**) malloc(sizeof(Node<T>*) * (MAX_LEVEL + 1));

    for(int i = 0; i < MAX_LEVEL + 1; ++i){
        next[i] = nullptr;
    }
}

template<typename T>
Node<T>::Node(T x, int height) : value(x), key(hash(x)), topLevel(height) {
    next = (Node<T>**) malloc(sizeof(Node<T>*) * (height + 1));

    for(int i = 0; i < MAX_LEVEL + 1; ++i){
        next[i] = nullptr;
    }
}

template<typename T>
Node<T>::~Node(){
    free(next);
}

}
