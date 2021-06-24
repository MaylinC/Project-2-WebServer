#ifndef __SIMPLE_WORK_QUEUE_HPP_
#define __SIMPLE_WORK_QUEUE_HPP_

#include<deque>
#include<pthread.h>

using namespace std;

struct work_queue {
    deque<long> jobs;
    pthread_mutex_t jobs_mutex;
    pthread_cond_t condition_variable = PTHREAD_COND_INITIALIZER;
    
    int add_job(long num) {
        pthread_mutex_lock(&this->jobs_mutex);
        jobs.push_back(num);
        size_t len = jobs.size();
        pthread_cond_signal(&condition_variable); 
        pthread_mutex_unlock(&this->jobs_mutex);
        return len;
    }
    
    bool remove_job(long *job) {
        pthread_mutex_lock(&this->jobs_mutex);
        bool success = !this->jobs.empty();
        if (success) {
            *job = this->jobs.front();
            this->jobs.pop_front();
        }
        pthread_mutex_unlock(&this->jobs_mutex);
        return success;
    }
};

#endif
