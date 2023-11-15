#include <iostream>
#include <sstream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#include <atomic>
#include <unordered_map>
#include <cstdlib> // For std::rand()
#include <ctime>   // For std::time()
std::mutex console_mutex; 

void safe_prints(const std::string& message) {
    std::lock_guard<std::mutex> guard(console_mutex);
    std::cout << message << std::endl;
}

template<typename SeparatorType, class... Ts>
void safe_print(const SeparatorType& sep, const Ts&... args) {
    std::ostringstream stream;
    const char* delim = "";
    (..., (stream << delim << args, delim = sep));
    safe_prints(stream.str());
}
// Define a structure for messages
struct message {
    enum class type { PROPOSE, VOTE, PRE_COMMIT, PRE_COMMIT_VOTE, COMMIT, COMMIT_VOTE, DECIDE, RESPONSE, STOP, NEW_VIEW, SYNC };
    type msg_type;
    std::string content;
    size_t sender_id;

    message(type mt, const std::string& cont, size_t sender)
        : msg_type(mt), content(cont), sender_id(sender) {}
};

// Define a class for message_queue
class message_queue {
    std::queue<message> queue;
    std::mutex mutex;
    std::condition_variable cond_var;

    public:
        void add_to_queue(const message& msg) {
            std::lock_guard<std::mutex> lock(mutex);
            queue.push(msg);
            cond_var.notify_one();
        }

        message pop_from_queue() {
        std::unique_lock<std::mutex> lock(mutex);
        while (queue.empty()) {
             cond_var.wait(lock);
        }
        message msg = queue.front();
        queue.pop();
        return msg;
    }
};

// Define the network_layer as a vector of message_queue pointers
using network_layer = std::vector<std::shared_ptr<message_queue>>;

// Helper function to convert message type to string
std::string message_type_to_string(message::type msg_type) {
    switch (msg_type) {
        case message::type::PROPOSE: return "PROPOSE";
        case message::type::VOTE: return "VOTE";
        case message::type::PRE_COMMIT: return "PRE_COMMIT";
        case message::type::PRE_COMMIT_VOTE: return "PRE_COMMIT_VOTE";
        case message::type::COMMIT: return "COMMIT";
        case message::type::COMMIT_VOTE: return "_VOTE";
        case message::type::DECIDE: return "DECIDE";
        case message::type::RESPONSE: return "RESPONSE";
        case message::type::STOP: return "STOP";
        default: return "UNKNOWN";
    }
}

class Replica {
public:
    enum State {
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
    std::atomic<int> votes;     // Count of votes received (for the leader)
    std::atomic<int> commit_votes; // Count of commit votes received (for the leader)
    bool is_leader;             // Indicates whether this replica is the current leader
    std::size_t leader_id;      // The ID of the current leader

    // Constructor
    Replica() : state(IDLE), votes(0), commit_votes(0), is_leader(false), leader_id(0) {}
};

// Helper function to send a message to all replicas except the sender
void broadcast_message(network_layer& network, const message& msg) {
    std::string msg_type_str = message_type_to_string(msg.msg_type);

    for (size_t i = 0; i < network.size(); ++i) {
        if (i != msg.sender_id) {
            network[i]->add_to_queue(msg);

            // Synchronized print for sent message
            // safe_print("Replica " + std::to_string(msg.sender_id) + " sent a " +
            //            msg_type_str + " message to Replica " + std::to_string(i) +
            //            ": " + msg.content);
            safe_print("Replica ", msg.sender_id, " sent a " ,
                       msg_type_str , " message to Replica ",  i ,
                       ": " , msg.content);
        }
    }
}

// The function that each bad replica will run
void replica_thread_function_evil_behaved(std::size_t replica_id, network_layer& network) {
    Replica replica;
    std::srand(static_cast<unsigned int>(std::time(nullptr))); // Seed the random number generator
    safe_print("Evil Replica " , replica_id, " started.");

    while (true) {
        message msg = network[replica_id]->pop_from_queue();

        // Synchronized print for received message
        safe_print("Evil Replica " , replica_id, " received a " ,
                   message_type_to_string(msg.msg_type) + " message from Replica " , msg.sender_id, ": " , msg.content);

        // Decide randomly whether to behave incorrectly or not
        if (std::rand() % 2) { // 50% chance to misbehave
            // Misbehave by broadcasting a wrong message or not broadcasting at all
            if (std::rand() % 2) {
                // Send a conflicting message
                message fake_msg(message::type::VOTE, "FakeContent", replica_id);
                network[replica.leader_id]->add_to_queue(fake_msg);
            }
            // Otherwise, do nothing (simulating a drop)
        } else {
            // Behave correctly (relay the message)
            //broadcast_message(network, msg);
            message vote_msg(message::type::VOTE, msg.content, replica_id);
            network[replica.leader_id]->add_to_queue(vote_msg);
        }

        if (msg.msg_type == message::type::STOP) {
            return; // Exit the loop and terminate the thread
        }
    }
}
void handle_unknown_message(const message& msg, std::size_t replica_id) {
    // Log the receipt of an unknown message
    safe_print("Replica ", replica_id, " received an unknown message of type: ", message_type_to_string(msg.msg_type));
}
void start_new_view(Replica& replica, network_layer& network, std::size_t replica_id, const std::string& new_leader_id) {
    // Start a new view, usually when the current leader is suspected to be faulty.

    // Set the state to VIEW_CHANGE
    {
        std::lock_guard<std::mutex> guard(console_mutex);
        replica.state = Replica::VIEW_CHANGE;
        replica.leader_id = std::stoull(new_leader_id);
        replica.is_leader = (replica_id == replica.leader_id);
        replica.votes = 0;
        replica.commit_votes = 0;
    }

    // Log the start of a new view
    safe_print("Replica ", replica_id, " starting new view with leader ", new_leader_id);
}

void sync_with_network(Replica& replica, network_layer& network, std::size_t replica_id) {
    // This function would typically involve a request to a distributed log or
    // state machine to get the latest committed value.
    // For the purpose of this example, it is a placeholder.

    // Placeholder for the actual committed value retrieved from the network or log.
    std::string latest_committed_value = "LatestCommittedValue";

    // Update the replica's state to match the latest committed state
    {
        std::lock_guard<std::mutex> guard(console_mutex);
        replica.state = Replica::IDLE;
        replica.value = latest_committed_value;
        replica.votes = 0;
        replica.commit_votes = 0;
    }

    // Log the synchronization for debugging purposes
    safe_print("Replica ", replica_id, " synchronized with the network: ", latest_committed_value);
}

void end_of_round(Replica& replica, std::size_t replica_id) {
    // Reset the replica's state at the end of a consensus round
    {
        std::lock_guard<std::mutex> guard(console_mutex);
        replica.state = Replica::IDLE;
        replica.votes = 0;
        replica.commit_votes = 0;
    }

    // Log the end of a round
    safe_print("Replica ", replica_id, " has ended the round and is now IDLE.");
}
// The function that each good replica thread will run
void replica_thread_function_well_behaved(std::size_t replica_id, network_layer& network) {
    // std::unordered_map<std::string, int> pre_votes_count;
    // std::unordered_map<std::string, int> pre_commits_count;
    Replica replica;
    const std::size_t n = network.size();
    const std::size_t f = (n - 1) / 3;  // Assuming 'n' is at least '3f + 1'

    // The quorum size is '2f + 1'
    const std::size_t QC = 2 * f + 1;
    safe_print("Replica " ,replica_id, " started.");
    //std::size_t leader_id = replica.leader_id; // This should be updated as the view changes
    std::size_t leader_id = 1;
    if (replica_id == 1) {
        // Simulate sending a proposal to all replicas to start the consensus process
        std::string proposal_value = "Transaction1";
        message proposal_msg(message::type::PROPOSE, proposal_value, replica_id);
        broadcast_message(network, proposal_msg);

        // After broadcasting the proposal, set this replica's state to the next appropriate state
        replica.state = Replica::State::PROPOSED;
    }

    while (true) {
        message msg = network[replica_id]->pop_from_queue();

        // Handle the message based on its type
        //PREPARE PHASE:
        switch (msg.msg_type) {
            case message::type::PROPOSE: {
                // If this is a leader's proposal and we're idle, move to prepared
                if (!replica.is_leader && replica.state == Replica::IDLE) {
                    replica.value = msg.content;
                    replica.state = Replica::PREPARED;
                    // Send a vote back to the leader
                    message vote_msg(message::type::VOTE, msg.content, replica_id);
                    network[leader_id]->add_to_queue(vote_msg);
                }
                break;
            }
            case message::type::VOTE: {
                // Leader collects votes from replicas
                if (replica.is_leader && replica.state == Replica::PREPARED) {
                    replica.votes++;
                    // Check if we have reached quorum
                    if (replica.votes >= QC) {
                        replica.state = Replica::PRE_COMMITTED;
                        // Broadcast pre-commit message to all replicas
                        message pre_commit_msg(message::type::PRE_COMMIT, replica.value, replica_id);
                        broadcast_message(network, pre_commit_msg);
                    }
                }
                break;
            }

            //PRE-COMMIT PHASE:
            case message::type::PRE_COMMIT: {
                // Replicas receive pre-commit message
                if (!replica.is_leader && replica.state == Replica::PREPARED) {
                    replica.state = Replica::COMMITTED;
                    // Send commit vote back to the leader
                    message commit_vote_msg(message::type::COMMIT, replica.value, replica_id);
                    network[leader_id]->add_to_queue(commit_vote_msg);
                }
                break;
            }

            //COMMIT PHASE
            case message::type::COMMIT: {
                // Leader collects commit votes
                if (replica.is_leader && replica.state == Replica::PRE_COMMITTED) {
                    replica.commit_votes++;
                    // Check if we have reached quorum
                    if (replica.commit_votes >= QC) {
                        replica.state = Replica::DECIDED;
                        // Broadcast decide message to all replicas
                        message decide_msg(message::type::DECIDE, replica.value, replica_id);
                        broadcast_message(network, decide_msg);
                    }
                }
                break;
            }
            //DECIDE PHASE
            case message::type::DECIDE: {
                // Replicas execute the decided value
                if (!replica.is_leader && replica.state == Replica::COMMITTED) {
                    replica.state = Replica::DECIDED;
                    // Here the value is actually applied/executed
                    // For example, updating a ledger or applying a state transition
                }
                break;
            }

            case message::type::SYNC: {
                // Handle synchronization logic
                if (replica.state == Replica::SYNCING) {
                // Process the sync message and update the state accordingly
                sync_with_network(replica, network, replica_id);
                }
                break;
            }

            case message::type::NEW_VIEW: {
                 // Handle the start of a new view
                if (replica.state == Replica::VIEW_CHANGE) {
                    // Process the new view message and update the state accordingly
                    std::string new_leader_id = msg.content;
                    start_new_view(replica, network, replica_id, new_leader_id);
                }
                break;
            }

            
            case message::type::STOP: {
                end_of_round(replica, replica_id);
                return; // Exit the thread loop
            }
            //UNHANDLED--> Any other issue 
            default: {
                // Handle other message types or unknown messages
                handle_unknown_message(msg, replica_id);
                break;
            }
        }
    }
}



// Main function to set up and run the simulation
int main() {
    std::cout << "hello world \n";
    std::size_t nr = 4; // Number of replicas

    std::vector<std::thread> replicas;
    network_layer queues(nr); // Initialize network layer with a number of replicas

    // Initialize the message queues for each replica
    for (std::size_t i = 0; i < nr; ++i) {
        queues[i] = std::make_shared<message_queue>();
    }

    // Start a thread for each replica
    for (std::size_t i = 0; i < nr; ++i) {
        if(i==0){
            // in this code replica 0 is bad replica
            replicas.emplace_back(replica_thread_function_evil_behaved, i, std::ref(queues));
        }else{
        replicas.emplace_back(replica_thread_function_well_behaved, i, std::ref(queues));
        }
    }

    // Allow threads to start and set up
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Simulate sending a proposal to all replicas to start the consensus process
    // message proposal_msg(message::type::PROPOSE, "Transaction1", 1); // Sender ID outside the range of replicas
    // broadcast_message(queues, proposal_msg);

    // Allow some time for the replicas to process the proposal
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Send a stop message to all replicas to end the simulation
    for (auto& queue : queues) {
        queue->add_to_queue(message(message::type::STOP, "", nr));
    }

    // Wait for all threads to complete
    for (auto& thread : replicas) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    return 0;
}
