#include <ros/ros.h>
#include <tf/transform_listener.h>
#include <vector>
#include <math.h>

// Frame transformations
#include "tf/transform_datatypes.h"
#include "tf_conversions/tf_eigen.h"
#include "Eigen/Core"
#include "Eigen/Geometry"

// WorldObject
#include "custom_msgs/WorldObject.h"

// Darknet
#include <darknet_ros_msgs/BoundingBoxes.h>

// PCL specific includes
#include <sensor_msgs/PointCloud2.h>
#include <visualization_msgs/Marker.h>
#include <geometry_msgs/Point.h>
#include <pcl_conversions/pcl_conversions.h>
#include "pcl_ros/transforms.h"
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/sample_consensus/model_types.h>
#include <pcl/sample_consensus/method_types.h>
#include <pcl/segmentation/sac_segmentation.h>
#include <pcl/filters/voxel_grid.h>

// NODE NAME 
std::string node_topic = "projector";

// FRAMES 
std::string ref_frame = "camera_rgb_optical_frame"; // Camera rgb frame
std::string global_frame = "map"; // Map frame
//std::string global_frame = "camera_link"; // Debug - test without SLAM

// POINTCLOUD TOPIC  
//std::string pointcloud_topic = "camera/depth/points"; // no rgb
std::string pointcloud_topic = "camera/depth_registered/points"; // rgb

// DETECTOR TOPIC   
std::string boxes_topic = "darknet_ros/bounding_boxes";

// OUT
std::string points_out = "camera/qhd/points_inliers";
std::string out_topic = "objects_raw";

bool use_mean = false;

// Astra camera
float camera_fx = 527.135883f;
float camera_fy = 527.76315129f;
float camera_cx = 306.5405905;
float camera_cy = 222.41208797f;

// Kinect V2
//float camera_fx = 1074.01f/2.0f;
//float camera_fy = 1073.9f/2.0f;
//float camera_cx = 945.3f/2.0f;
//float camera_cy = 537.4f/2.0f;

typedef pcl::PointCloud<pcl::PointXYZRGB> PointCloudRGB;

class Projector
{
    public:
        Projector(ros::NodeHandle * node_handle, std::string ref_frame, std::string global_frame, std::string pointcloud_topic, std::string boxes_topic, std::string out_topic);
        //MOD
        //custom_msgs::WorldObject process_cloud(std::string class_name, pcl::PointCloud<pcl::PointXYZ> obj_cloud, int xmin, int xmax, int ymin, int ymax);
        custom_msgs::WorldObject process_cloud(std::string class_name, pcl::PointCloud<pcl::PointXYZRGB> obj_cloud, int xmin, int xmax, int ymin, int ymax);
        pcl::PointXYZ pointFromUV(float A, float B, float C, float D, float fx, float fy, float cx, float cy, float u, float v);

    private:

        tf::TransformListener * listener;
        ros::NodeHandle * nh;
        //MOD
        //pcl::PointCloud<pcl::PointXYZ> cloud_buffer;
        pcl::PointCloud<pcl::PointXYZRGB> cloud_buffer;

        // Subscribers
        ros::Subscriber cloud_sub;
        ros::Subscriber boxes_sub; 

        // Publishers
        ros::Publisher obj_pub;
        ros::Publisher cloud_pub;

        // Debug variables
        ros::Publisher vis_pub;
        int count; 
        void markArrow(pcl::PointXYZ start, pcl::PointXYZ end, std::string frame);

        // Callbacks
        void boxes_callback(const darknet_ros_msgs::BoundingBoxes::ConstPtr & boxes_ptr);
        void cloud_callback(const sensor_msgs::PointCloud2ConstPtr & cloud2_ptr);
};

Projector::Projector(ros::NodeHandle * node_handle, std::string ref_frame, std::string global_frame, std::string pointcloud_topic, std::string boxes_topic, std::string out_topic)
{
    nh = node_handle;
    listener = new tf::TransformListener;
    count = 0;

    // Initialize Subscribers
    cloud_sub = nh->subscribe(pointcloud_topic, 1, &Projector::cloud_callback, this);
    boxes_sub = nh->subscribe(boxes_topic, 1, &Projector::boxes_callback, this);

    // Initialize Publishers
    obj_pub = nh->advertise<custom_msgs::WorldObject>(out_topic, 1);
    cloud_pub = nh->advertise<sensor_msgs::PointCloud2>(points_out, 0);
    //cloud_pub = nh->advertise<PointCloudRGB>(points_out, 0);

    // Initialize Debug variables
    vis_pub = nh->advertise<visualization_msgs::Marker>( "raw_marker", 0 );
}

//MOD
//custom_msgs::WorldObject Projector::process_cloud(std::string class_name, pcl::PointCloud<pcl::PointXYZ> obj_cloud, int xmin, int xmax, int ymin, int ymax)
custom_msgs::WorldObject Projector::process_cloud(std::string class_name, pcl::PointCloud<pcl::PointXYZRGB> obj_cloud, int xmin, int xmax, int ymin, int ymax)
{
    // Convert from RGBXYZ to XYZ
    pcl::PointCloud<pcl::PointXYZ> obj_cloud_xyz;
    pcl::copyPointCloud(obj_cloud,obj_cloud_xyz);

    custom_msgs::WorldObject obj;
    obj.objClass = class_name;

    pcl::PointCloud<pcl::PointXYZRGB> inlier_cloud;
    //pcl::fromROSMsg( cloud2, cloud);
    //cloud_buffer = pcl::PointCloud<pcl::PointXYZ>(cloud);

    // door
    if (class_name == "door")
    {
        // Apply RANSAC in ref_frame
        pcl::ModelCoefficients coefficients;
        pcl::PointIndices inliers;

        // Create the segmentation object
        pcl::SACSegmentation<pcl::PointXYZ> seg;

        // Optional
        seg.setOptimizeCoefficients (true);

        // Mandatory
        seg.setModelType (pcl::SACMODEL_PLANE);
        seg.setMethodType (pcl::SAC_RANSAC);
        seg.setDistanceThreshold (0.03);
        //MOD
        //seg.setInputCloud (obj_cloud.makeShared());
        seg.setInputCloud (obj_cloud_xyz.makeShared());
        seg.segment (inliers, coefficients); 
        inlier_cloud.width = inliers.indices.size();
        inlier_cloud.height = 1;
        inlier_cloud.resize(inlier_cloud.width);
        for(int i = 0; i < inliers.indices.size(); i++)
        {
            int index = inliers.indices[i];
            inlier_cloud.points[i] = obj_cloud.points[index];
        }

        // Publish cloud
        sensor_msgs::PointCloud2 inliers_msg;
        pcl::toROSMsg(inlier_cloud, inliers_msg);
        inliers_msg.header.frame_id = global_frame;
        cloud_pub.publish(inliers_msg);

        ROS_INFO_STREAM("\nInliers count: "+ std::to_string( inliers.indices.size()));
        pcl::PointXYZ obj_position;

        // 1. Calculates object location in ref_frame:         
        // Object location is the mean of points inside bounding box
        if(use_mean)
        {
            double mean_ratio = 1.0f/inliers.indices.size();
            for(int i = 0; i < inliers.indices.size(); i++)
            {
                int index = inliers.indices.at(i);
                obj_position.x += obj_cloud.at(index).x*mean_ratio;
                obj_position.y += obj_cloud.at(index).y*mean_ratio;
                obj_position.z += obj_cloud.at(index).z*mean_ratio;
            }
        }

        // Object location is the middle point between two projections of bounding boxes into the plane found
        if(!use_mean)
        {
            float u1 = (float) xmin;
            float v1 = (float) ymin;

            float u2 = (float) xmax;
            float v2 = (float) ymax;

            float v_mean =(v1+v2)*0.5f;

            // Left projection
            pcl::PointXYZ p1 = pointFromUV(coefficients.values[0], coefficients.values[1], coefficients.values[2], coefficients.values[3], camera_fx, camera_fy, camera_cx, camera_cy, u1, v_mean);
            pcl::PointXYZ p2 = pointFromUV(coefficients.values[0], coefficients.values[1], coefficients.values[2], coefficients.values[3], camera_fx, camera_fy, camera_cx, camera_cy, u2, v_mean);
            pcl::PointXYZ p_middle;
            p_middle.x = (p1.x+p2.x)/2.0f;
            p_middle.y = (p1.y+p2.y)/2.0f;
            p_middle.z = (p1.z+p2.z)/2.0f;

            obj_position = p_middle;
        }
        
        // Convert location and normal of object from ref_frame to global_frame
        tf::StampedTransform transform;
        try{
            ros::Time now = ros::Time::now();
            listener->waitForTransform(global_frame, ref_frame, now, ros::Duration(3.0));
            listener->lookupTransform(global_frame, ref_frame, ros::Time(0), transform);
        }
        catch(tf::TransformException ex)
        {
            ROS_ERROR("Error transforming point\n!");            
            ROS_ERROR("%s",ex.what());
            ros::Duration(1.0).sleep();
        }

        // Rotation matrix and translation vector
        tf::Matrix3x3 rot_tf = transform.getBasis();
        tf::Vector3 trans_tf = transform.getOrigin();
        
        // optional: use eigen for matrix multiplication?
        Eigen::Vector3d obj_pos_eigen(obj_position.x, obj_position.y, obj_position.z);
        Eigen::Matrix3d Rot;
        Eigen::Vector3d Trans;
        tf::matrixTFToEigen(rot_tf, Rot);
        tf::vectorTFToEigen(trans_tf, Trans);

        //ROS_INFO_STREAM("\nRot matrix " << Rot);

        // Rotate and translate position
        obj_pos_eigen = Rot * obj_pos_eigen;
        obj_pos_eigen = obj_pos_eigen + Trans;
        
        // Set position back from eigen
        obj_position.x = obj_pos_eigen(0);
        obj_position.y = obj_pos_eigen(1);
        obj_position.z = obj_pos_eigen(2);

        // Apply rotation to normal
        Eigen::Vector3d obj_normal(coefficients.values[0], coefficients.values[1], coefficients.values[2]);
        obj_normal = Rot * obj_normal;

        // Plot normal
        if(coefficients.values.size() == 4)
        {
            pcl::PointXYZ start, end;
            start = obj_position;
            end = obj_position;
            end.x += obj_normal(0) * 0.8;
            end.y += obj_normal(1) * 0.8;
            end.z += obj_normal(2) * 0.8;
            markArrow(start, end, global_frame);
        }

        // Calculates object angle in ref_frame (in XY plane):
        // It is the angle of -normal in XY plane (Z is upwards in usual map frame)
        float x = obj_normal(0);
        float y = obj_normal(1);
        double angle = atan2(y, x);
        obj.x = obj_position.x;
        obj.y = obj_position.y;
        obj.angle = angle;
    }

    else
    {
        ROS_INFO_STREAM("\nUnimplemented");
    }

    return obj;
}

// Projects the (u,v) image point into the plane and find 3D point
pcl::PointXYZ Projector::pointFromUV(float A, float B, float C, float D, float fx, float fy, float cx, float cy, float u, float v)
{
    pcl::PointXYZ p;

    p.z = -(D*fx*fy)/(fy*A*(u-cx) + fx*B*(v-cy) + C*fx*fy);
    p.x = (u - cx)*p.z/fx;
    p.y = (v - cy)*p.z/fy;

    return p;
}


void Projector::boxes_callback(const darknet_ros_msgs::BoundingBoxes::ConstPtr & boxes_ptr)
{
    count++; 
    ROS_INFO_STREAM("\nReceived box "+std::to_string( count));
    int box_num = boxes_ptr->bounding_boxes.size();

    for(int i = 0; i < box_num; i++)
    {
        // Object class
        std::string class_name = boxes_ptr->bounding_boxes.at(i).Class;
        custom_msgs::WorldObject object; 

        // Object boundaries
        int xmin = boxes_ptr->bounding_boxes.at(i).xmin;
        int ymin = boxes_ptr->bounding_boxes.at(i).ymin;
        int xmax = boxes_ptr->bounding_boxes.at(i).xmax;
        int ymax = boxes_ptr->bounding_boxes.at(i).ymax;

        // Crop pointcloud inside bounding box
        //MOD
        //pcl::PointCloud<pcl::PointXYZ> cropped; 
        pcl::PointCloud<pcl::PointXYZRGB> cropped; 
        cropped.width = std::abs(xmax-xmin);
        cropped.height = std::abs(ymax-ymin);
        cropped.points.resize (cropped.width * cropped.height);

        //ROS_INFO_STREAM("\nPointcloud dim: "+ std::to_string(cloud_buffer.width) + " "+ std::to_string(cloud_buffer.height));

        for(int x = 0; x < cropped.width; x++)
            for(int y = 0; y < cropped.height; y++)
                cropped.at(x, y) = cloud_buffer.at(x+xmin, y+ymin);

        

        // Process data in core function to generate a world object
        object = process_cloud(class_name, cropped, xmin, xmax, ymin, ymax);

        // Publish encountered object
        obj_pub.publish(object);

        ROS_INFO_STREAM("\nProcessed box "+std::to_string( count));
    }
}

void Projector::cloud_callback(const sensor_msgs::PointCloud2ConstPtr & cloud2_ptr)
{
    // Transform pointcloud frame 
    sensor_msgs::PointCloud2 cloud2;
    try{
        ros::Time now = ros::Time::now();
        listener->waitForTransform("camera_rgb_optical_frame", ref_frame, now, ros::Duration(10.0) );
        //listener->waitForTransform("camera_rgb_optical_frame", ref_frame, ros::Time(0), ros::Duration(10.0) );
        pcl_ros::transformPointCloud(ref_frame, *cloud2_ptr, cloud2, *listener);

    } catch (tf::TransformException ex) {
        ROS_ERROR("Error transforming cloud\n!");        
        ROS_ERROR("%s",ex.what());
    }

    // Transform pointcloud type and save to buffer
    //MOD
    //pcl::PointCloud<pcl::PointXYZ> cloud;
    pcl::PointCloud<pcl::PointXYZRGB> cloud;
    pcl::fromROSMsg( cloud2, cloud);
    //MOD
    //cloud_buffer = pcl::PointCloud<pcl::PointXYZ>(cloud);
    cloud_buffer = pcl::PointCloud<pcl::PointXYZRGB>(cloud);
}

void Projector::markArrow(pcl::PointXYZ start, pcl::PointXYZ end, std::string frame)
{
    visualization_msgs::Marker marker;

    std::vector<geometry_msgs::Point> points(2);
    points[0].x = start.x;
    points[0].y = start.y;
    points[0].z = start.z;
    points[1].x = end.x;
    points[1].y = end.y;
    points[1].z = end.z;

    marker.header.frame_id = frame;
    marker.header.stamp = ros::Time();
    marker.ns = "arrows";
    marker.id = 0;
    marker.type = visualization_msgs::Marker::ARROW;
    marker.action = visualization_msgs::Marker::ADD;
    marker.scale.x = 0.02;
    marker.scale.y = 0.05;
    marker.scale.z = 0.1;
    marker.color.a = 1.0; 
    marker.color.r = 0.0;
    marker.color.g = 1.0;
    marker.color.b = 0.0;
    marker.points = points;

    vis_pub.publish( marker );
}

int main (int argc, char** argv)
{

    // Initialize ROS
    ros::init (argc, argv, node_topic);
    ros::NodeHandle * nh = new ros::NodeHandle;

    Projector proj(nh, ref_frame, global_frame, pointcloud_topic, boxes_topic, out_topic);
    
    // Spin
    ros::spin ();
}
