#pragma once

#include <vector>
#include <stdexcept>

namespace gen { // Start Generic namespace

    template<class T>
    struct CV_Elem {

        static const int FILLED = -2;
        static const int EOL    = -1;

        const int index; //Own index in vector

        T data;

        int next_empty; // -1 = null, -2 = filled              

        //'Structors:
        CV_Elem() = delete;

        CV_Elem(int index_, int *fe)
            : index(index_) { // Empty init

            next_empty = *fe;

            *fe = index;

            }

        CV_Elem(int index_, T data_)
            : index(index_)
            , data(data_) { // Filled init

            next_empty = -2;

            }            

        CV_Elem(const CV_Elem & other) = default; // Needs to be copyable because it will be in a vector
        CV_Elem& operator=(const CV_Elem & other) = default;

        CV_Elem(CV_Elem && other) = default; // Move ctors to speed things up
        CV_Elem& operator=(CV_Elem && other) = default;

        ~CV_Elem() = default;

        //Utility:
        bool empty() const {

            return (next_empty != FILLED);

            }

        };

    // =============================================== //

    template<class T, class Alloc = std::allocator<T>> 
    class ConsVec {

        private:

            int first_empty;

            std::vector<CV_Elem<T>, Alloc> elem_vec;

        public:

            ConsVec()
                : first_empty(CV_Elem<T>::EOL)
                , elem_vec(1, CV_Elem<T>(0, &first_empty)) {

                }

            ConsVec(size_t n)
                : ConsVec() {

                if (n <= 1) return;

                elem_vec.reserve(n);

                }

            size_t insert(const T & data) {

                if (first_empty == CV_Elem<T>::EOL) {

                    size_t s = elem_vec.size();

                    elem_vec.push_back( CV_Elem<T>(s, data) );

                    return s;

                    }
                else {

                    size_t index = first_empty;

                    first_empty = elem_vec[index].next_empty;

                    elem_vec[index].data = data;
                    elem_vec[index].next_empty = -2;

                    return index;

                    }

                }

            T &  at(size_t n) {
                
                if (n >= elem_vec.size()) throw std::out_of_range("");

                if (elem_vec[n].empty()) throw std::logic_error("");

                return elem_vec[n].data;

                }

            T &  at(size_t n) const {

                if (n >= elem_vec.size()) throw std::out_of_range("");

                if (elem_vec[n].empty()) throw std::logic_error("");

                return elem_vec[n].data;

                }

            void mark_empty(size_t n) {
                
                if (n >= elem_vec.size()) throw std::out_of_range("");

                if (elem_vec[n].empty()) return; 

                elem_vec[n].next_empty = first_empty;

                first_empty = n;
                
                }

            void mark_all_empty() {
                
                first_empty = CV_Elem<T>::EOL;

                for (size_t i = elem_vec.size() - 1; i >= 0; i -= 1) {
                    
                    elem_vec[i].next_empty = first_empty;

                    first_empty = i;
                    
                    }

                }

            void clear() {
                
                elem_vec.resize(1);

                elem_vec[0].next_empty = CV_Elem<T>::EOL;

                first_empty = 0;
                
                }

            void reserve(size_t n) {

                elem_vec.reserve(n);
                
                }

            void shrink_to_fit() {
                
                size_t i;

                for (i = elem_vec.size() - 1; i >= 0; i -= 1) {
                    
                    if (!elem_vec[n].empty()) break;
                    
                    }

                if (i == 0) return;

                elem_vec.resize(i + 1);
                
                first_empty = CV_Elem<T>::EOL;

                for (i = elem_vec.size() - 1; i >=0; i -= 1) {

                    if (elem_vec[i].empty()) {

                        elem_vec[i].next_empty = first_empty;

                        first_empty = i;

                        }

                    }

                }

            size_t size() const {
                
                return elem_vec.size();
                
                }

            size_t capacity() const {
                
                return elem_vec.capacity();
                
                }

            bool empty_at(size_t n) const {

                if (n >= elem_vec.size()) throw std::out_of_range("");
                
                return elem_vec[n].empty();

                }

            T & operator[](size_t n) {
                
                return at(n);
                
                }

            T & operator[](size_t n) const {

                return at(n);

                }

        };

    } // End Generic namespace