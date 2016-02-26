#include <obstacle_distance/obstacle_distance.h>

class CreateCollisionWorld : public collision_detection::CollisionWorldFCL {
public:
    CreateCollisionWorld(const collision_detection::WorldPtr &world) :
            CollisionWorldFCL(world) {
    }

    void getCollisionObject(std::vector<boost::shared_ptr<fcl::CollisionObject> > &obj) {
        std::map<std::string, collision_detection::FCLObject>::iterator it = fcl_objs_.begin();
        obj.reserve(fcl_objs_.size());

        for (it; it != fcl_objs_.end(); ++it) {
            obj.push_back(it->second.collision_objects_[0]);
        }
    }
};

class CreateCollisionRobot : public collision_detection::CollisionRobotFCL {
public:
    CreateCollisionRobot(const robot_model::RobotModelConstPtr &model) :
            CollisionRobotFCL(model) {
    }

    void getCollisionObject(const robot_state::RobotState &state,
                            std::vector<boost::shared_ptr<fcl::CollisionObject> > &obj) {
        collision_detection::FCLObject fcl_obj;
        constructFCLObject(state, fcl_obj);
        obj = fcl_obj.collision_objects_;
    }
};

void ObstacleDistance::updatedScene(planning_scene_monitor::PlanningSceneMonitor::SceneUpdateType type) {

    planning_scene_monitor::LockedPlanningSceneRO ps(planning_scene_monitor_);
    planning_scene::PlanningScenePtr planning_scene_ptr = ps->diff();

    CreateCollisionWorld collision_world(planning_scene_ptr->getWorldNonConst());
    robot_state::RobotState robot_state(planning_scene_ptr->getCurrentState());

    CreateCollisionRobot collision_robot(robot_state.getRobotModel());
    std::vector<boost::shared_ptr<fcl::CollisionObject> > robot_obj, world_obj;

    collision_robot.getCollisionObject(robot_state, robot_obj);
    collision_world.getCollisionObject(world_obj);

    robot_links_list.clear();
    collision_objects_list.clear();
    kinematic_list.empty();

    for (int i = 0; i < robot_obj.size(); i++) {
        const collision_detection::CollisionGeometryData *robot_link =
                static_cast<const collision_detection::CollisionGeometryData *>(robot_obj[i]->collisionGeometry()->getUserData());
        robot_links_list[robot_link->getID()] = robot_obj[i];
        kinematic_list.push_back(robot_link->getID());
    }
    for (int i = 0; i < world_obj.size(); i++) {
        const collision_detection::CollisionGeometryData *collision_object =
                static_cast<const collision_detection::CollisionGeometryData *>(world_obj[i]->collisionGeometry()->getUserData());
        collision_objects_list[collision_object->getID()] = world_obj[i];
    }
}

bool ObstacleDistance::registerCallback(cob_srvs::SetString::Request &req,
                                        cob_srvs::SetString::Response &res) {

    boost::mutex::scoped_lock lock(registered_links_mutex_);
    std::pair<std::set<std::string>::iterator, bool> ret = registered_links_.insert(req.data);

    res.success = true;
    res.message = (ret.second) ? (req.data + " successfully registered") : (req.data + " already registered");
    return true;
}

bool ObstacleDistance::unregisterCallback(cob_srvs::SetString::Request &req,
                                          cob_srvs::SetString::Response &res) {

    boost::mutex::scoped_lock lock(registered_links_mutex_);
    std::set<std::string>::iterator it = registered_links_.find(req.data);
    
    if (it != registered_links_.end())
    {
        registered_links_.erase(it);

        res.success = true;
        res.message = req.data + " successfully unregistered";
    }
    else
    {
        res.success = true;
        res.message = req.data + " has not been registered before";
    }

    return true;
}

void ObstacleDistance::calculateDistances(const ros::TimerEvent& event) {

    std::map<std::string, boost::shared_ptr<fcl::CollisionObject> > robot_links_list = this->robot_links_list;
    std::map<std::string, boost::shared_ptr<fcl::CollisionObject> > collision_objects_list = this->collision_objects_list;

    boost::mutex::scoped_lock lock(registered_links_mutex_);
    obstacle_distance::DistanceInfos distance_infos;
    
    std::set<std::string>::iterator link_it;
    for (link_it = registered_links_.begin(); link_it!=registered_links_.end(); ++link_it)
    {
        std::map<std::string, boost::shared_ptr<fcl::CollisionObject> >::iterator obj_it;
        for (obj_it = collision_objects_list.begin(); obj_it != collision_objects_list.end(); ++obj_it) {
            obstacle_distance::DistanceInfo info;
            info.header.stamp = event.current_real;
            info.link_of_interest = *link_it;
            info.obstacle_id = obj_it->first;
            info.distance = getMinimalDistance(*link_it, obj_it->first, robot_links_list, collision_objects_list);
            distance_infos.infos.push_back(info);
        }
    }

    distance_pub_.publish(distance_infos);
}



bool ObstacleDistance::calculateDistanceCallback(obstacle_distance::GetObstacleDistance::Request &req,
                                                 obstacle_distance::GetObstacleDistance::Response &resp) {
    std::map<std::string, boost::shared_ptr<fcl::CollisionObject> > robot_links_list = this->robot_links_list;
    std::map<std::string, boost::shared_ptr<fcl::CollisionObject> > collision_objects_list = this->collision_objects_list;
    std::vector<std::string> kinematic_list = this->kinematic_list;

    // Chains
    for (unsigned int c=0; c< req.chains.size(); ++c) {
        // Link chain to objects
        bool start = false;
        for (int i = 0; i < kinematic_list.size(); i++) {
            if (!start && kinematic_list[i] == req.chains[c].chain_base) start = true;
            if (start) {
                if (req.objects.size() == 0) {
                    // All objects
                    std::map<std::string, boost::shared_ptr<fcl::CollisionObject> >::iterator it;
                    for (it = collision_objects_list.begin(); it != collision_objects_list.end(); ++it) {
                        resp.link_to_object.push_back(kinematic_list[i] + "_to_" + it->first);
                        resp.distances.push_back(ObstacleDistance::getMinimalDistance(kinematic_list[i], it->first,
                                                                                      robot_links_list, collision_objects_list));
                    }
                } else {
                    // Specific objects
                    for (int y = 0; y < req.objects.size(); y++) {
                        resp.link_to_object.push_back(kinematic_list[i] + " to " + req.objects[y]);
                        resp.distances.push_back(ObstacleDistance::getMinimalDistance(kinematic_list[i], req.objects[y],
                                                                                      robot_links_list, collision_objects_list));
                    }
                }
                if (kinematic_list[i] == req.chains[c].chain_tip) break;
            }
        }
    }
    
    // Links
    for (unsigned int i=0; i< req.links.size(); ++i) {
        if (req.objects.size() == 0) {
            // All objects
            std::map<std::string, boost::shared_ptr<fcl::CollisionObject> >::iterator it;
            for (it = collision_objects_list.begin(); it != collision_objects_list.end(); ++it) {
                resp.link_to_object.push_back(req.links[i] + "_to_" + it->first);
                resp.distances.push_back(ObstacleDistance::getMinimalDistance(req.links[i], it->first,
                                                                              robot_links_list, collision_objects_list));
            }
        } else {
            // Specific objects
            for (int y = 0; y < req.objects.size(); y++) {
                resp.link_to_object.push_back(req.links[i] + " to " + req.objects[y]);
                resp.distances.push_back(ObstacleDistance::getMinimalDistance(req.links[i], req.objects[y],
                                                                              robot_links_list, collision_objects_list));
            }
        }
    }
    return true;
}

double ObstacleDistance::getMinimalDistance(std::string robot_link_name,
                                            std::string collision_object_name,
                                            std::map<std::string, boost::shared_ptr<fcl::CollisionObject> > robot_links,
                                            std::map<std::string, boost::shared_ptr<fcl::CollisionObject> > collision_objects) {
    fcl::DistanceResult res;
    res.update(MAXIMAL_MINIMAL_DISTANCE, NULL, NULL, fcl::DistanceResult::NONE, fcl::DistanceResult::NONE);

    double dist = fcl::distance(robot_links[robot_link_name].get(),
                                collision_objects[collision_object_name].get(),
                                fcl::DistanceRequest(),
                                res);
    if (dist < 0) dist = 0;
    return dist;
}

ObstacleDistance::ObstacleDistance() {
    MAXIMAL_MINIMAL_DISTANCE = 5.0; //m
    double update_frequency = 100.0; //Hz
    bool error = false;

    std::string robot_description = "/robot_description";
    std::string distance_service = "/calculate_distance";
    std::string register_service = "/register_links";
    std::string unregister_service = "/unregister_links";
    std::string distance_topic = "/obstacle_distances";

    //Initialize planning scene monitor
    boost::shared_ptr<tf::TransformListener> tf_listener_(new tf::TransformListener(ros::Duration(2.0)));
    try {
        planning_scene_monitor_ = boost::make_shared<planning_scene_monitor::PlanningSceneMonitor>(robot_description, tf_listener_);
    } catch (ros::InvalidNameException) {
        error = true;
    }

    calculate_obstacle_distance_ = nh_.advertiseService(distance_service, &ObstacleDistance::calculateDistanceCallback, this);
    register_server_ = nh_.advertiseService(register_service, &ObstacleDistance::registerCallback, this);
    unregister_server_ = nh_.advertiseService(unregister_service, &ObstacleDistance::unregisterCallback, this);
    distance_timer_ = nh_.createTimer(ros::Duration(0.1), &ObstacleDistance::calculateDistances, this);
    distance_pub_ = nh_.advertise<obstacle_distance::DistanceInfos>(distance_topic, 1);

    if (!error) {
        planning_scene_monitor_->setStateUpdateFrequency(update_frequency);
        planning_scene_monitor_->startSceneMonitor();
        planning_scene_monitor_->startWorldGeometryMonitor();
        planning_scene_monitor_->startStateMonitor();
        planning_scene_monitor_->addUpdateCallback(boost::bind(&ObstacleDistance::updatedScene, this, _1));

        ROS_INFO("%s: Node started!", ros::this_node::getName().c_str());
    } else
        ROS_ERROR("%s: Node failed!", ros::this_node::getName().c_str());

    registered_links_.clear();
    distance_infos_ = obstacle_distance::DistanceInfos();
}

int main(int argc, char **argv) {
    ros::init(argc, argv, "obstacle_distance_node");

    ObstacleDistance ob;
    ros::spin();

    ros::shutdown();
    return 0;
}
