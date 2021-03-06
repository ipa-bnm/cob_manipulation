#! /usr/bin/env python
import rospy

import actionlib
import moveit_msgs.msg
import cob_grasp_generation.msg

def query_grasps_client():
    client = actionlib.SimpleActionClient('query_grasps', cob_grasp_generation.msg.QueryGraspsAction)
    client.wait_for_server()

    goal = cob_grasp_generation.msg.QueryGraspsGoal()
    #goal.object_name="peanuts"
    #goal.gripper_type = "sdh"
    goal.object_name="pringles"
    goal.gripper_type = "sdhx"
    #goal.grasp_id = 2
    goal.num_grasps = 0
    goal.threshold = 0

    client.send_goal(goal)
    client.wait_for_result()
    return client.get_result()

if __name__ == '__main__':
    try:
        rospy.init_node('query_grasps_client')
        result = query_grasps_client()
        print "Result:"
        print result
        #print len(result.grasp_list)
    except rospy.ROSInterruptException:
        print "program interrupted before completion"
