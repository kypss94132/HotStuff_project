#define JELLE_SAID_NO 0  // this is false, so we can use #if JELLE_SAID to exclude old parts I changed :)

#include <iostream>
#include <optional>
#include <sstream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#include <ctime>   // For std::time()
std::mutex console_mutex; 

void safe_prints(const std::string& message) {
    std::lock_guard<std::mutex> guard(console_mutex);
    std::cout << message << std::endl;
}
template<typename T>
std::ostream& operator<<(std::ostream& os, const std::optional<T>& opt) {
    if (opt) {
        os << *opt; // Print the value if it exists
    } else {
        os << "nullopt"; // Or a placeholder if it doesn't
    }
    return os;
}
template<typename SeparatorType, class... Ts>
void safe_print(const SeparatorType& sep, const Ts&... args) {
    std::ostringstream stream;
    const char* delim = "";
    ((stream << delim << args, delim = sep), ...); // Modified fold expression
    safe_prints(stream.str());
}
// Define a structure for messages
struct message {
    enum class type {
        PREPARE, VOTE,
        PRE_COMMIT, PRE_COMMIT_VOTE,
        COMMIT, COMMIT_VOTE,
        DECIDE, RESPONSE,
        STOP, NEW_VIEW, SYNC
    };

    type msg_type;
    std::optional<std::string> content;
    std::size_t sender_id;
    std::size_t view_number;

    message(type msg_type, const std::optional<std::string>& content, std::size_t sender_id, std::size_t view_number)
        : msg_type(msg_type), content(content), sender_id(sender_id), view_number(view_number) {}
};

// Define a class for message_queue
class message_queue {
    std::queue<message> queue;
    std::mutex mutex;
    std::condition_variable cond_var;

public:
    void add_to_queue(const message& msg)
    {
        //std::lock_guard<std::mutex> lock(mutex);
        queue.push(msg);
        //cond_var.notify_one();
    }

    message pop_from_queue()
    {
        //std::unique_lock<std::mutex> lock(mutex);
        //while (queue.empty()) {
        //cond_var.wait(lock);
            
        //}
        message msg = queue.front();
        queue.pop();
        return msg;
    }
    
    bool empty() const {
        return queue.empty();
    }
};
 //Client Queue is created.
class client_queue {
    std::queue<std::string> queue; // Queue to store client requests as strings
    std::mutex mutex;
    std::condition_variable cond_var;

public:
    void add_to_queue(const std::string& request) {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push(request);
        cond_var.notify_one();
    }

    std::optional<std::string> pop_from_queue() {
        std::unique_lock<std::mutex> lock(mutex);
        if (queue.empty()) {
            return std::nullopt; // Return an empty optional if the queue is empty
        }
        std::string request = queue.front();
        queue.pop();
        return request; // Return the popped value wrapped in an optional
    }

    bool empty() const {
        return queue.empty();
    }

    std::optional<std::string> front_of_queue() {
        if (queue.empty()) {
            return std::nullopt; // Return an empty optional if the queue is empty
        }
        std::string request = queue.front();
        return request; // Return the popped value wrapped in an optional
    }

};


// Define the network_layer as a vector of message_queue pointers
using network_layer = std::vector<message_queue>;
client_queue global_client_queue;


/*
 * Return a string literal representation for the message type @{msg_type}.
 */
std::string_view to_string(message::type msg_type)
{
    //using enum message::type;
    switch (msg_type) {
        case message::type::PREPARE:
            return "PREPARE";
        case message::type::VOTE:
            return "VOTE";
        case message::type::PRE_COMMIT:
            return "PRE_COMMIT";
        case message::type::PRE_COMMIT_VOTE:
            return "PRE_COMMIT_VOTE";
        case message::type::COMMIT:
            return "COMMIT";
        case message::type::COMMIT_VOTE:
            return "COMMIT_VOTE";
        case message::type::DECIDE:
            return "DECIDE";
        case message::type::RESPONSE:
            return "RESPONSE";
        case message::type::STOP:
            return "STOP";
        default:
            /* This should not happen? Should we detect that? */
            // assert(false)
            return "UNKNOWN";
    }
}

/*
 * Write the message type @{msg_type} to output.
 */
std::ostream& operator<<(std::ostream& out, message::type msg_type)
{
    return out << to_string(msg_type);
}


/*
 * struct Replica represents the internal state of the replica right?
 *
 * Then why not something like this:
 */
#if 0 // so that the compiler ignores it
struct replica_state
{
    /* Identifier of this replica, never changes. */
    const std::size_t replica_id;

    /* The current round number. */
    std::size_t round_number; 

    /* The current state of this replica in round @{round}/ */
    state round_state;
    
    /* Messages proposed & accepted in previous rounds. */
    std::vector<message> ledger;
};
#endif

/*
 * Instead of the following.
 */

struct Replica {
    /* Lets make this an enum class. */
    enum class State {
        IDLE,
        PROPOSED,        // Initial state, waiting for proposal
        PREPARED,    // Has received a proposal and sent a vote
        PRE_COMMITTED, // Has received enough votes to pre-commit
        COMMITTED,   // Has received a pre-commit and sent a commit vote
        DECIDED, // Has received enough commit votes to decide
        LOCKED,
        SYNCING,
        VIEW_CHANGE
    };

    State state;
    std::string value;          // The value being proposed/committed/etc.

    std::atomic<int> votes = {0};
    std::atomic<int> pre_commit_votes = {0}; 
    std::atomic<int> commit_votes = {0}; 
    
    bool is_leader = false;             
    std::size_t replica_id;     
    std::size_t round_number = -1;
};

// Helper function to send a message to all replicas except the sender
void broadcast_message(network_layer& network, const message& msg) {
    for (size_t i = 0; i < network.size(); ++i) {
        if (i != msg.sender_id) { //Not including itself
            network[i].add_to_queue(msg);

            // Synchronized print for sent message
            // safe_print("Replica " + std::to_string(msg.sender_id) + " sent a " +
            //            msg_type_str + " message to Replica " + std::to_string(i) +
            //            ": " + msg.content);
            //safe_print("Replica ", msg.sender_id, " sent a " ,
            //           msg.msg_type , " message to Replica ",  i ,
            //           ": " , msg.content);
            std::cout << "Broadcasting Message : Sender ID : " << msg.sender_id << " Type : " << msg.msg_type << " to Replica : " << i << " Content : " << msg.content << std::endl;
        }
    }
}

void handle_unknown_message(const message& msg, std::size_t replica_id) {
    // Log the receipt of an unknown message
    safe_print("Replica ", replica_id, " received an unknown message of type: ", msg.msg_type);
}

void start_new_view(Replica& replica, network_layer& network, std::size_t replica_id, std::size_t round_number)
{
    std::size_t new_leader_id = round_number % network.size();
    safe_print("New View started!");
    /* the new_leader_id should be a number here---lets not mix ``interpreting message'' and updating internal state. */

    // Start a new view, usually when the current leader is suspected to be faulty.

    
    #if JELLE_SAID_NO
        std::lock_guard<std::mutex> guard(console_mutex); /// ????
    
        /* If you need a lock here for some reason, then use a purpose-specific lock. *//
    #endif
    
    
    // replica.state = Replica::State::VIEW_CHANGE;
    //replica.replica_id = new_leader_id;

    replica.is_leader = (replica_id == new_leader_id);
    replica.votes = 0;
    replica.commit_votes = 0;

    // Log the start of a new view
    //safe_print("Replica ", replica_id, " starting new view with leader ", new_leader_id);
}


void end_of_round(Replica& replica, std::size_t replica_id) {
    // Reset the replica's state at the end of a consensus round
    #if JELLE_SAID_NO
        std::lock_guard<std::mutex> guard(console_mutex); /// ????
    
        /* If you need a lock here for some reason, then use a purpose-specific lock. *//
    #endif

    
    replica.state = Replica::State::IDLE;
    replica.votes = 0;
    replica.commit_votes = 0;

    // Log the end of a round
    //safe_print("Replica ", replica_id, " has ended the round and is now IDLE.");
}


class ConsensusCertificate {
public:
    std::vector<message> messages;

    ConsensusCertificate(const std::vector<message>& messages) : messages(messages) {}
    ConsensusCertificate() {}

    bool is_valid() const {
        // Implement logic to validate the certificate
        return !messages.empty(); // Simplified check
    }
};

ConsensusCertificate receive_certificate(network_layer& network, Replica& replica, message::type certificate_type, std::string content) {
    std::vector<message> collected_messages;
    const std::size_t n = network.size();
    const std::size_t f = (n - 1) / 3;
    const std::size_t required_messages = n - f;

    while (collected_messages.size() != required_messages) {
        if(!network[replica.replica_id].empty())
        {
            message msg = network[replica.replica_id].pop_from_queue();
            std::cout << "Popped Message from Leader Queue MSG Content : " << msg.content << "Sender ID : " << msg.sender_id << std::endl;
            if (msg.msg_type == certificate_type && msg.view_number == replica.round_number && msg.content == content) {
                collected_messages.push_back(msg);
            }
            else
            {
                
                return ConsensusCertificate();
            }
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        }
        // Additional checks and logic can be added as needed
    }

    return ConsensusCertificate(collected_messages);
}

bool receive_message(message& msg, size_t& leader_id, Replica& replica, message::type expected_type) {

        if(msg.msg_type == expected_type                    && 
           leader_id == msg.sender_id                       && 
           msg.view_number == replica.round_number          &&
           global_client_queue.front_of_queue() == msg.content)
        {

            return true;
        }
        return false;
}


/* Leader or round changes every second---we should not need to provide that. */
void replica_thread_function_well_behaved(std::size_t replica_id, network_layer& network)
{
    // using enum Replica::State;
    

    Replica replica;
    // std::size_t leader_id = 1;
    // if(replica.replica_id == leader_id){ //???
    //    replica.is_leader = true;
    // }
    // std::cout << "replica is leader : " << replica.is_leader << "\n" ;
    
    replica.replica_id = replica_id;
    const std::size_t n = network.size();
    const std::size_t f = (n - 1) / 3;  // Assuming 'n' is at least '3f + 1'

    const std::size_t QC = n - f;
    std::cout << "Replica " << replica_id << " started." << std::endl;
    //safe_print("Replica ", replica_id, " started.");
    //std::size_t leader_id = replica.leader_id; // This should be updated as the view changes

    replica.round_number = replica.round_number + 1;

    #if 0
    //std::size_t leader_id = 1;
    if (replica.is_leader) {
        // Simulate sending a proposal to all replicas to start the consensus process
        safe_print("view: ", replica.round_number, "has started");
        std::string proposal_value = "Transaction1";
        message proposal_msg(message::type::PREPARE, proposal_value, replica_id, replica.round_number);
        broadcast_message(network, proposal_msg);

        // After broadcasting the proposal, set this replica's state to the next appropriate state
        replica.state = Replica::State::PREPARED;
    }
    #endif

    while (true) {
#if 1 // disabled, to make it behave like comment.

        /* Maybe change to something like this: */
        

        auto leader_id = replica.round_number % n;
        
        // std::cout << leader_id <<"\n";
        if (replica.round_number % n == replica_id) {
            std::cout << "Leader: " << replica.round_number % n << "\n";
            auto request = global_client_queue.front_of_queue();
            
            
            
            

            std::cout << request << "\n";
            // auto newv = receive_certificate(network, replica, message::type::NEW_VIEW, *request);

                
            if(request){
                auto start = std::chrono::steady_clock::now();
                if(replica.round_number != 0)
                {
                    auto vc = receive_certificate(network, replica, message::type::NEW_VIEW, *request);
                    global_client_queue.pop_from_queue();

                    while(global_client_queue.empty()) 
                    {
                        
                    }
                    request = global_client_queue.front_of_queue();

                    if(!vc.is_valid())
                    {
                        // Call View Change
                        start_new_view(
                            replica,
                            network,
                            replica_id,
                            replica.round_number+1
                            );
                    }
                }

                message proposal_msg(message::type::PREPARE, *request, replica_id, replica.round_number);
                std::cout << "Primary has got a view change request." << std::endl;
                broadcast_message(network, proposal_msg);
                auto vc = receive_certificate(network, replica, message::type::PREPARE, *request);
                if(!vc.is_valid())
                {
                    // Call View Change
                    start_new_view(
                        replica,
                        network,
                        replica_id,
                        replica.round_number+1
                        );
                }

                // receive_vote_Certificate can ignore all messages that are not proper vote messages: HotStuff is sequential.
                message pre_commit_msg(message::type::PRE_COMMIT, request, replica.round_number % n, replica.round_number);
                broadcast_message(network, pre_commit_msg);
                vc = receive_certificate(network, replica, message::type::PRE_COMMIT, *request);
                if(!vc.is_valid())
                {
                    // Call View Change
                    start_new_view(
                        replica,
                        network,
                        replica_id,
                        replica.round_number+1
                        );
                }
                std::cout << "Primary has validated PRE-COMMIT requests." << std::endl;
                message commit_msg(message::type::COMMIT, request, replica.round_number % n, replica.round_number);
                broadcast_message(network, commit_msg);
                vc = receive_certificate(network, replica, message::type::COMMIT, *request);
                if(!vc.is_valid())
                {
                    // Call View Change
                    start_new_view(
                        replica,
                        network,
                        replica_id,
                        replica.round_number+1
                        );
                }
                std::cout << "Primary has validated COMMIT requests." << std::endl;
                message decide_msg(message::type::DECIDE, request, replica.round_number % n, replica.round_number);
                broadcast_message(network, decide_msg);
                replica.round_number = replica.round_number + 1;

                auto end = std::chrono::steady_clock::now();
                std::chrono::duration<double> elapsed_seconds = end - start;
                std::cout << "Round time: " << elapsed_seconds.count() << "s\n";


                
            // I am the leader.
            // auto newv = receive_certificate(network, replica, message::type::NEW_VIEW, request);
            // try{
            //     // auto request = global_client_queue.pop_from_queue();

                
            // }
            // }
            // catch (const std::exception& e) {

            //     std:: cout << "Exception: ";
            // } 
            
           
            }

            }
        else {
            /* Am not the leader, wait for proposal from leader. */
            if(!network[replica_id].empty())
            {
                message msg = network[replica_id].pop_from_queue();
                //safe_print("Message " , msg.content, "-", msg.sender_id , "\n");
                //auto proposal = receive_certificate(network, replica, message::type::PREPARE, *msg.content);
                

                // Check for states and change accordingly
                auto validMessageFromPrimary = receive_message(msg, leader_id, replica, message::type::PREPARE);
                if(validMessageFromPrimary)
                {    
                    /* send vote for proposal to leader. */
                    std::cout <<"R" << replica_id << " has validated the PREPARE request"<< std::endl;
                    message pre_vote_msg(message::type::PREPARE, msg.content, replica_id, replica.round_number);
                    network[leader_id].add_to_queue(pre_vote_msg);
                }
                else
                {
                    //start_new_view();
                }
                validMessageFromPrimary = receive_message(msg, leader_id, replica, message::type::PRE_COMMIT);
                if(validMessageFromPrimary)
                {    
                    /* send vote for proposal to leader. */
                    std::cout << "R" << replica_id << " has validated the PRE_COMMIT request"  << std::endl;
                    message pre_vote_msg(message::type::PRE_COMMIT, msg.content, replica_id, replica.round_number);
                    network[leader_id].add_to_queue(pre_vote_msg);
                }
                else
                {
                    //start_new_view();
                }
                validMessageFromPrimary = receive_message(msg, leader_id, replica, message::type::COMMIT);
                if(validMessageFromPrimary)
                {    
                    /* send vote for proposal to leader. */
                    std::cout << "R" << replica_id << " has validated the COMMIT request"<< std::endl;
                    message pre_vote_msg(message::type::COMMIT, msg.content, replica_id, replica.round_number);
                    network[leader_id].add_to_queue(pre_vote_msg);
                }
                else
                {
                    //start_new_view();
                }
                validMessageFromPrimary = receive_message(msg, leader_id, replica, message::type::DECIDE);
                if(validMessageFromPrimary)
                {    
                    /* send vote for proposal to leader. */
                    std::cout << "R" << replica_id << " has validated the DECIDE request"<< std::endl;
                    std::cout << "Transaction is executed on R" << replica_id << std::endl;
                    replica.round_number = replica.round_number + 1;
                    leader_id = replica.round_number % n;
                    message new_view_msg(message::type::NEW_VIEW, msg.content, replica_id, replica.round_number);
                    network[leader_id].add_to_queue(new_view_msg);
                
                }
            

            }
            else
            {

                std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            }
            // auto proposal = receive_certificate(network, replica, message::type::PREPARE, message.content);
            // /* send vote for proposal to leader. */
            // message pre_vote_msg(message::type::PREPARE, request, replica_id, replica.round_number);
            // network[leader_id].add_to_queue(pre_vote_msg);


            // /* wait for vote certificate from leader. */
            // auto vcc = receive_certificate(network, replica, message::type::PREPARE, request);
            // /* send pre-commit for proposal to leader. */
            // message pre_commit_vote(message::type::PRE_COMMIT_VOTE, request, replica_id, replica.round_number);
            // network[leader_id].add_to_queue(pre_commit_vote);

            // /* wait for pre-commit certificate from leader. */
            // auto pccc = receive_certificate(network, replica, message::type::PRE_COMMIT, request);
            //  /* send commit for proposal to leader. */
            // message commit_vote(message::type::COMMIT, request, replica_id, replica.round_number);
            // network[leader_id].add_to_queue(commit_vote);

           
            // /* wait for commit certificate from leader. */
            // auto ccc = receive_certificate(network, replica, message::type::COMMIT, request);
        }
        
        
#endif
    
    }
}



// Main function to set up and run the simulation
int main() {
    const std::size_t nr = 4; // Number of replicas, is constant.
    //const std::size_t leader_id = 1; ???

    std::vector<std::thread> replicas;
    network_layer queues(nr); // Initialize network layer with a number of replicas

    #if JELLE_SAID_NO
        /* The above line already makes @{nr} queues. */
    // Initialize the message queues for each replica
    for (std::size_t i = 0; i < nr; ++i) {
        queues[i] = std::make_shared<message_queue>();
    }
    #endif

    global_client_queue.add_to_queue("T1");
    global_client_queue.add_to_queue("T2");
    global_client_queue.add_to_queue("T3");
    global_client_queue.add_to_queue("T4");
    
    
    for(std::size_t i = 0; i < 4; ++i){
        #if JELLE_SAID_NO
            /* lets make normal-case work first. */
            if (i==0) {
                // in this code replica 0 is bad replica
                replicas.emplace_back(replica_thread_function_evil_behaved, i, std::ref(queues), 0, 1);
            }
            else {
        #endif
        replicas.emplace_back(replica_thread_function_well_behaved, i, std::ref(queues));
    }
        
    // Allow threads to start and set up
    std::this_thread::sleep_for(std::chrono::milliseconds(100000));

    // Simulate sending a proposal to all replicas to start the consensus process
  //  for (std::size_t i = 0; i < 100; ++i) {
  //      message proposal_msg{
  //          .msg_type = message::type::PROPOSE,
  //          .content = "Transaction " + std::to_string(i),
  //          .sender_id = 5, // outside the range of replicas: replicas are 0, 1, 2, 4.
  //          .view_number = i // i-th transaction
  //      };
  //      (queues, proposal_msg);
  //      std::this_thread::sleep_for(std::chrono::seconds(5));
  //  }


    // Send a stop message to all replicas to end the simulation
    // message stop_msg {
    //     .msg_type = message::type::STOP,
    //     .content = "",
    //     .sender_id = 5, // outside the range of replicas: replicas are 0, 1, 2, 4.
    //     .view_number = 101 // after last transaction.
    // };

    //  message initial_proposal(message::type::NEW_VIEW, "NEW_VIEW", 0, 0);
    //  queues[0].add_to_queue(initial_proposal);

    // broadcast_message(queues, stop_msg);



}
