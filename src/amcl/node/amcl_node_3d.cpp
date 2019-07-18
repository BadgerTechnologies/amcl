/*
 *
 *       AMCL Octomap Class
 *        by Tyler Buchman
 *        2019
 *
 */


#include "map.h"
#include "octomap.h"
#include "amcl_odom.h"
#include "amcl_node.h"
#include "amcl/AMCLConfig.h"

// roscpp
#include "ros/ros.h"
#include "ros/assert.h"

#include "octomap_msgs/Octomap.h"
#include "octomap_msgs/conversions.h"
#include <boost/bind.hpp>

#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_types.h>
#include <pcl/PCLPointCloud2.h>
#include <pcl/conversions.h>
#include <pcl_ros/transforms.h>

using namespace amcl;

void
AmclNode::init3D()
{
  lidar_ = NULL;
  last_lidar_data_ = NULL;
  private_nh_.param("lidar_max_beams", max_beams_, 256);
  private_nh_.param("lidar_z_hit", z_hit_, 0.95);
  private_nh_.param("lidar_z_rand", z_rand_, 0.05);
  private_nh_.param("lidar_sigma_hit", sigma_hit_, 0.2);
  private_nh_.param("lidar_off_map_factor", off_map_factor_, 1.0);
  private_nh_.param("lidar_non_free_space_factor", non_free_space_factor_, 1.0);
  private_nh_.param("lidar_non_free_space_radius", non_free_space_radius_, 0.0);
  private_nh_.param("lidar_likelihood_max_dist", sensor_likelihood_max_dist_, 0.36);
  private_nh_.param("global_localization_lidar_off_map_factor", global_localization_off_map_factor_, 1.0);
  private_nh_.param("global_localization_lidar_non_free_space_factor", global_localization_non_free_space_factor_, 1.0);
  private_nh_.param("lidar_height", lidar_height_, 1.8);
  private_nh_.param("off_object_penalty_factor", off_object_penalty_factor_, 1.0);
  std::string tmp_model_type;
  private_nh_.param("lidar_model_type", tmp_model_type, std::string("lidar"));
  if(tmp_model_type == "lidar")
  {
    lidar_model_type_ = LIDAR_MODEL;
  }
  else
  {
    ROS_WARN("Unknown lidar model type \"%s\"; defaulting to lidar model",
             tmp_model_type.c_str());
    lidar_model_type_ = LIDAR_MODEL;
  }

  lidar_scan_sub_ = new message_filters::Subscriber<sensor_msgs::PointCloud2>(nh_, scan_topic_, 1);
  lidar_scan_filter_ =
          new tf::MessageFilter<sensor_msgs::PointCloud2>(*lidar_scan_sub_, *tf_, odom_frame_id_, 1);
  lidar_scan_filter_->registerCallback(boost::bind(&AmclNode::lidarReceived, this, _1));
  // 15s timer to warn on lack of receipt of lidar scans, #5209
  lidar_check_interval_ = ros::Duration(15.0);
  check_lidar_timer_ = nh_.createTimer(lidar_check_interval_, boost::bind(&AmclNode::checkLidarReceived, this, _1));

  try
  {
    this->tf_->waitForTransform("base_footprint", "top_laser", ros::Time::now(), ros::Duration(5.0));
    this->tf_->lookupTransform("base_footprint", "top_laser", ros::Time::now(), lidar_to_footprint_tf_);
  }
  catch(tf::TransformException& e)
  {
    ROS_ERROR("failed to get top laser to base footprint transform.");
    return;
  }
}

void
AmclNode::checkLidarReceived(const ros::TimerEvent& event)
{
  ros::Duration d = ros::Time::now() - last_lidar_received_ts_;
  if(d > lidar_check_interval_)
  {
    ROS_WARN("No lidar scan received (and thus no pose updates have been published) for %f seconds. "
             "Verify that data is being published on the %s topic.",
             d.toSec(),
             ros::names::resolve(scan_topic_).c_str());
  }
}

/**
 * Convert a octomap message into the internal
 * representation.  This allocates an OctoMap and returns it.
 */
OctoMap*
AmclNode::convertMap(const octomap_msgs::Octomap& map_msg)
{
    OctoMap* map = new OctoMap();
    octomap::AbstractOcTree* absoctree;
    octomap::OcTree* octree;
    ROS_ASSERT(map);
    double scale = map_msg.resolution;
    bool binary = map_msg.binary;
    if(binary)
    {
      absoctree = octomap_msgs::binaryMsgToMap(map_msg);
    }
    else
    {
      // ROS_ERROR("OcTree message not in binary format. Cannot read data into OcTree");
      // return nullptr;
      absoctree = octomap_msgs::fullMsgToMap(map_msg);
    }
    if(absoctree)
    {
      octree = dynamic_cast<octomap::OcTree*>(absoctree);
    }
    map->setScale(scale);
    map->initFromOctree(octree, lidar_height_);
    return map;
}

#include <stdlib.h>
double
AmclNode::scorePose3D(const PFVector &p)
{
  if(this->last_lidar_data_ == NULL)
  {
    // There is no data to match, so return a perfect match
    return 1.0;
  }
  // Create a fake "sample set" of just this pose to score it.
  pf_sample_t fake_sample;
  fake_sample.pose.v[0] = p.v[0];
  fake_sample.pose.v[1] = p.v[1];
  fake_sample.pose.v[2] = p.v[2];
  fake_sample.weight = 1.0;
  pf_sample_set_t fake_sample_set;
  fake_sample_set.sample_count = 1;
  fake_sample_set.samples = &fake_sample;
  fake_sample_set.converged = 0;
  AMCLLidar::ApplyModelToSampleSet(this->last_lidar_data_, &fake_sample_set);
  if(rand()%1000 == 0)
      ROS_INFO("p0: %f, p1: %f, p2: %f, weight: %f", p.v[0], p.v[1], p.v[2], fake_sample.weight);
  return fake_sample.weight;
}

void
AmclNode::reconfigure3D(amcl::AMCLConfig &config)
{
  sensor_min_range_ = config.lidar_min_range;
  sensor_max_range_ = config.lidar_max_range;
  max_beams_ = config.lidar_max_beams;
  z_hit_ = config.lidar_z_hit;
  z_short_ = config.lidar_z_short;
  z_max_ = config.lidar_z_max;
  z_rand_ = config.lidar_z_rand;
  sigma_hit_ = config.lidar_sigma_hit;
  sensor_likelihood_max_dist_ = config.lidar_likelihood_max_dist;
  off_map_factor_ = config.lidar_off_map_factor;
  non_free_space_factor_ = config.lidar_non_free_space_factor;
  non_free_space_radius_ = config.lidar_non_free_space_radius;
  global_localization_off_map_factor_ = config.global_localization_lidar_off_map_factor;
  global_localization_non_free_space_factor_ = config.global_localization_lidar_non_free_space_factor;
  off_object_penalty_factor_ = config.off_object_penalty_factor;
  lidar_height_ = config.lidar_height;
  if(config.lidar_model_type == "lidar")
  {
    lidar_model_type_ = LIDAR_MODEL;
  }
  delete lidar_;
  lidar_ = new AMCLLidar(max_beams_, (OctoMap*)map_, lidar_height_);
  ROS_ASSERT(lidar_);
  if(lidar_model_type_ == LIDAR_MODEL)
  {
    ROS_WARN("setting model lidar type from reconfigure 3d");
    lidar_->SetModelLidarType(z_hit_, z_rand_, sigma_hit_, sensor_likelihood_max_dist_);
  }
  lidar_->SetMapFactors(off_map_factor_, non_free_space_factor_, non_free_space_radius_);
  delete lidar_scan_filter_;
  lidar_scan_filter_ = new tf::MessageFilter<sensor_msgs::PointCloud2>(*lidar_scan_sub_, *tf_,
                                                                     odom_frame_id_, 100);
  lidar_scan_filter_->registerCallback(boost::bind(&AmclNode::lidarReceived, this, _1));
}

void
AmclNode::octoMapReceived(const octomap_msgs::OctomapConstPtr& msg)
{
  if( first_map_only_ && first_map_received_ ) {
    return;
  }

  boost::recursive_mutex::scoped_lock cfl(configuration_mutex_);
  ROS_DEBUG("Received a new Octomap");
  freeMapDependentMemory();
  // Clear queued lidar objects because they hold pointers to the existing map
  lidars_.clear();
  lidars_update_.clear();
  frame_to_lidar_.clear();
  delete last_lidar_data_;
  last_lidar_data_ = NULL;

  map_ = convertMap(*msg);
  initFromNewMap();

  first_map_received_ = true;
}

#include <stdlib.h>
void
AmclNode::initFromNewMap3D()
{
  delete lidar_;
  lidar_ = new AMCLLidar(max_beams_, (OctoMap*)map_, lidar_height_);
  ROS_ASSERT(lidar_);
  if(lidar_model_type_ == LIDAR_MODEL)
  {
    ROS_WARN("setting model lidar type from init from new map 3d");
    lidar_->SetModelLidarType(z_hit_, z_rand_, sigma_hit_, sensor_likelihood_max_dist_);
  }
  lidar_->SetMapFactors(off_map_factor_, non_free_space_factor_, non_free_space_radius_);

  // Index of free space
  // Must be calculated after the occ_dist is setup by the laser model
  free_space_indices.resize(0);
  std::vector<int> size_vec = map_->getSize();
  ROS_INFO("non free space radius: %f", non_free_space_radius_);
  for(int i = 0; i < size_vec[0]; i++)
    for(int j = 0; j < size_vec[1]; j++)
      //if(map_->isValid({i, j}) and ((OctoMap*)map_)->getOccDist(i, j) > non_free_space_radius_)
      if(map_->isValid({i, j}))
      {
        free_space_indices.push_back(std::make_pair(i,j));
        /*
        if(rand() % 1000 == 0)
          ROS_INFO("free space indices: %d, %d", i, j);
        */
      }
}

void
AmclNode::lidarReceived(const sensor_msgs::PointCloud2ConstPtr& lidar_scan)
{
  last_lidar_received_ts_ = ros::Time::now();
  if(map_ == NULL) {
    ROS_DEBUG("map is null");
    return;
  }

  boost::recursive_mutex::scoped_lock cfl(configuration_mutex_);
  int lidar_index = -1;

  // Handle corner cases like getting dynamically reconfigured or getting a
  // new map by de-activating the global localization parameters here if we are
  // no longer globally localizing.
  if(!global_localization_active_)
  {
    pf_->alpha_slow = alpha_slow_;
    pf_->alpha_fast = alpha_fast_;
    lidar_->SetMapFactors(off_map_factor_, non_free_space_factor_, non_free_space_radius_);
    for (auto& l : lidars_)
    {
      l->SetMapFactors(off_map_factor_, non_free_space_factor_, non_free_space_radius_);
    }
  }

  // Do we have the base->base_lidar Tx yet?
  if(frame_to_lidar_.find(lidar_scan->header.frame_id) == frame_to_lidar_.end())
  {
    lidars_.push_back(new AMCLLidar(*lidar_));
    lidars_update_.push_back(true);
    lidar_index = frame_to_lidar_.size();

    tf::Stamped<tf::Pose> ident (tf::Transform(tf::createIdentityQuaternion(),
                                             tf::Vector3(0,0,0)),
                                 ros::Time(), lidar_scan->header.frame_id);
    tf::Stamped<tf::Pose> lidar_pose;
    try
    {
      this->tf_->transformPose(base_frame_id_, ident, lidar_pose);
    }
    catch(tf::TransformException& e)
    {
      ROS_ERROR("Couldn't transform from %s to %s, "
                "even though the message notifier is in use",
                lidar_scan->header.frame_id.c_str(),
                base_frame_id_.c_str());
      return;
    }

    PFVector lidar_pose_v;
    lidar_pose_v.v[0] = lidar_pose.getOrigin().x();
    lidar_pose_v.v[1] = lidar_pose.getOrigin().y();
    // lidar mounting angle gets computed later -> set to 0 here!
    lidar_pose_v.v[2] = 0;
    lidars_[lidar_index]->SetLidarPose(lidar_pose_v);
    lidars_[lidar_index]->SetLidarToFootprintTF(lidar_to_footprint_tf_);
    ROS_DEBUG("Received lidar's pose wrt robot: %.3f %.3f %.3f",
              lidar_pose_v.v[0],
              lidar_pose_v.v[1],
              lidar_pose_v.v[2]);

    frame_to_lidar_[lidar_scan->header.frame_id] = lidar_index;
  } else {
    // we have the lidar pose, retrieve lidar index
    lidar_index = frame_to_lidar_[lidar_scan->header.frame_id];
  }

  // Where was the robot when this scan was taken?
  PFVector pose;
  if(!getOdomPose(latest_odom_pose_, pose.v[0], pose.v[1], pose.v[2],
                  lidar_scan->header.stamp, base_frame_id_))
  {
    ROS_ERROR("Couldn't determine robot's pose associated with lidar scan");
    return;
  }

  PFVector delta;
  if(pf_init_)
  {
    // Compute change in pose
    delta.v[0] = pose.v[0] - pf_odom_pose_.v[0];
    delta.v[1] = pose.v[1] - pf_odom_pose_.v[1];
    delta.v[2] = angle_diff(pose.v[2], pf_odom_pose_.v[2]);

    // See if we should update the filter
    bool update;
    if (odom_integrator_topic_.size()) {
      double abs_trans = sqrt(odom_integrator_absolute_motion_.v[0]*odom_integrator_absolute_motion_.v[0] +
                              odom_integrator_absolute_motion_.v[1]*odom_integrator_absolute_motion_.v[1]);
      double abs_rot = odom_integrator_absolute_motion_.v[2];
      update = abs_trans >= d_thresh_ || abs_rot >= a_thresh_;
    } else {
      update = fabs(delta.v[0]) > d_thresh_ ||
               fabs(delta.v[1]) > d_thresh_ ||
               fabs(delta.v[2]) > a_thresh_;
    }
    update = update || m_force_update;
    m_force_update=false;

    // Set the lidar update flags
    // TODO: force update with update variable
    //update = true;
    if(update)
      for(unsigned int i=0; i < lidars_update_.size(); i++)
        lidars_update_[i] = true;
  }

  bool force_publication = false;
  if(!pf_init_)
  {
    // Pose at last filter update
    pf_odom_pose_ = pose;

    // Filter is now initialized
    pf_init_ = true;

    // Should update sensor data
    for(unsigned int i=0; i < lidars_update_.size(); i++)
      lidars_update_[i] = true;

    force_publication = true;

    resample_count_ = 0;

    initOdomIntegrator();
  }
  else if(pf_init_ && lidars_update_[lidar_index])
  {
    AMCLOdomData odata;
    odata.pose = pose;
    // HACK
    // Modify the delta in the action data so the filter gets
    // updated correctly
    odata.delta = delta;
    odata.absolute_motion = odom_integrator_absolute_motion_;
    if (odom_integrator_topic_.size())
    {
      geometry_msgs::Pose2D p;
      p.x = odata.absolute_motion.v[0];
      p.y = odata.absolute_motion.v[1];
      p.theta = odata.absolute_motion.v[2];
      absolute_motion_pub_.publish(p);
    }

    // Use the action data to update the filter
    odom_->UpdateAction(pf_, (AMCLSensorData*)&odata);

    resetOdomIntegrator();
  }

  bool resampled = false;
  // If the robot has moved, update the filter
  if(lidars_update_[lidar_index])
  {
    delete last_lidar_data_;
    last_lidar_data_ = new AMCLLidarData;
    AMCLLidarData &ldata = *last_lidar_data_;
    ldata.sensor = lidars_[lidar_index];
    ldata.frame_id = lidar_scan->header.frame_id;
    pcl::PCLPointCloud2 pc2;
    pcl_conversions::toPCL(*lidar_scan, pc2);
    pcl::PointCloud<pcl::PointXYZ>::Ptr point_cloud(new pcl::PointCloud<pcl::PointXYZ>);
    pcl::fromPCLPointCloud2(pc2, *point_cloud);

    // sample point cloud
    int max_beams = lidars_[lidar_index]->getMaxBeams();
    int data_count = point_cloud->size();
    int step = (data_count - 1) / (max_beams - 1);
    step = std::max(step, 1);
    // Sample lidar scan to max_beams number of points
    for(int i=0; i<data_count; i+=step)
    {
      pcl::PointXYZ point = point_cloud->at(i);
      ldata.points.push_back(point);
    }
    ldata.points.header = point_cloud->header;
    // TODO: scrutenize this section
    lidars_[lidar_index]->UpdateSensor(pf_, (AMCLSensorData*)&ldata);
    lidars_update_[lidar_index] = false;
    pf_odom_pose_ = pose;
    // Resample the particles
    if(!(++resample_count_ % resample_interval_))
    {
      // TODO: scrutenize this section
      pf_->update_resample();
      resampled = true;
      if(pf_->converged && global_localization_active_)
      {
        ROS_INFO("Global localization converged!");
        global_localization_active_ = false;
      }
    }

    pf_sample_set_t* set = pf_->sets + pf_->current_set;
    // Publish the resulting cloud
    // TODO: set maximum rate for publishing
    if (!m_force_update) {
      geometry_msgs::PoseArray cloud_msg;
      cloud_msg.header.stamp = ros::Time::now();
      cloud_msg.header.frame_id = global_frame_id_;
      cloud_msg.poses.resize(set->sample_count);
      for(int i=0;i<set->sample_count;i++)
      {
        tf::poseTFToMsg(tf::Pose(tf::createQuaternionFromYaw(set->samples[i].pose.v[2]),
                                 tf::Vector3(set->samples[i].pose.v[0],
                                           set->samples[i].pose.v[1], 0)),
                        cloud_msg.poses[i]);
      }
      particlecloud_pub_.publish(cloud_msg);
      if (global_alt_frame_id_.size() > 0)
      {
        geometry_msgs::PoseArray alt_cloud_msg(cloud_msg);
        alt_cloud_msg.header.frame_id = global_alt_frame_id_;
        alt_particlecloud_pub_.publish(alt_cloud_msg);
      }
    }
  }

  if(resampled || force_publication)
  {
    // Read out the current hypotheses
    double max_weight = 0.0;
    int max_weight_hyp = -1;
    std::vector<amcl_hyp_t> hyps;
    hyps.resize(pf_->sets[pf_->current_set].cluster_count);
    for(int hyp_count = 0;
        hyp_count < pf_->sets[pf_->current_set].cluster_count; hyp_count++)
    {
      double weight;
      PFVector pose_mean;
      PFMatrix pose_cov;
      if (!pf_->get_cluster_stats(hyp_count, &weight, &pose_mean, &pose_cov))
      {
        ROS_ERROR("Couldn't get stats on cluster %d", hyp_count);
        break;
      }

      hyps[hyp_count].weight = weight;
      hyps[hyp_count].pf_pose_mean = pose_mean;
      hyps[hyp_count].pf_pose_cov = pose_cov;

      if(hyps[hyp_count].weight > max_weight)
      {
        max_weight = hyps[hyp_count].weight;
        max_weight_hyp = hyp_count;
      }
    }

    if(max_weight > 0.0)
    {
      geometry_msgs::PoseWithCovarianceStamped p;
      // Fill in the header
      p.header.frame_id = global_frame_id_;
      p.header.stamp = lidar_scan->header.stamp;
      // Copy in the pose
      p.pose.pose.position.x = hyps[max_weight_hyp].pf_pose_mean.v[0];
      p.pose.pose.position.y = hyps[max_weight_hyp].pf_pose_mean.v[1];
      tf::quaternionTFToMsg(tf::createQuaternionFromYaw(hyps[max_weight_hyp].pf_pose_mean.v[2]),
                            p.pose.pose.orientation);
      // Copy in the covariance, converting from 3-D to 6-D
      pf_sample_set_t* set = pf_->sets + pf_->current_set;
      for(int i=0; i<2; i++)
      {
        for(int j=0; j<2; j++)
        {
          // Report the overall filter covariance, rather than the
          // covariance for the highest-weight cluster
          p.pose.covariance[6*i+j] = set->cov.m[i][j];
        }
      }
      // Report the overall filter covariance, rather than the
      // covariance for the highest-weight cluster
      p.pose.covariance[6*5+5] = set->cov.m[2][2];

      pose_pub_.publish(p);
      last_published_pose = p;
      if (global_alt_frame_id_.size() > 0)
      {
        geometry_msgs::PoseWithCovarianceStamped alt_p(p);
        alt_p.header.frame_id = global_alt_frame_id_;
        alt_pose_pub_.publish(alt_p);
      }

      // subtracting base to odom from map to base and send map to odom instead
      tf::Stamped<tf::Pose> odom_to_map;
      try
      {
        tf::Transform tmp_tf(tf::createQuaternionFromYaw(hyps[max_weight_hyp].pf_pose_mean.v[2]),
                             tf::Vector3(hyps[max_weight_hyp].pf_pose_mean.v[0],
                                         hyps[max_weight_hyp].pf_pose_mean.v[1],
                                         0.0));
        tf::Stamped<tf::Pose> tmp_tf_stamped (tmp_tf.inverse(),
                                              lidar_scan->header.stamp,
                                              base_frame_id_);
        this->tf_->waitForTransform(base_frame_id_, odom_frame_id_,
                                    lidar_scan->header.stamp, ros::Duration(1.0));
        this->tf_->transformPose(odom_frame_id_,
                                 tmp_tf_stamped,
                                 odom_to_map);
      }
      catch(tf::TransformException e)
      {
        ROS_WARN("Failed to subtract base to odom transform: %s", e.what());
        return;
      }

      try
      {
        boost::recursive_mutex::scoped_lock tfl(tf_mutex_);
        latest_tf_ = tf::Transform(tf::Quaternion(odom_to_map.getRotation()),
                                   tf::Point(odom_to_map.getOrigin()));
        latest_tf_valid_ = true;
      }
      catch(tf::TransformException)
      {
        ROS_WARN("Failed to transform odom to map pose");
        return;
      }
    }
    else
    {
      ROS_ERROR("No pose!");
    }
  }
  else if(latest_tf_valid_)
  {

    // Is it time to save our last pose to the param server
    ros::Time now = ros::Time::now();
    if((save_pose_to_server_period.toSec() > 0.0) &&
       (now - save_pose_to_server_last_time) >= save_pose_to_server_period)
    {
      ROS_DEBUG("time to save pose to server: %f", save_pose_to_server_period.toSec());
      this->savePoseToServer();
      save_pose_to_server_last_time = now;
    }
    if((save_pose_to_file_period.toSec() > 0.0) &&
       (now - save_pose_to_file_last_time) >= save_pose_to_file_period)
    {
      ROS_DEBUG("time to save pose to file: %f", save_pose_to_file_period.toSec());
      this->savePoseToFile();
      save_pose_to_file_last_time = now;
    }
  }

}

void
AmclNode::globalLocalizationCallback3D()
{
  lidar_->SetMapFactors(global_localization_off_map_factor_,
                        global_localization_non_free_space_factor_,
                        non_free_space_radius_);
  for (auto& l : lidars_)
  {
    l->SetMapFactors(global_localization_off_map_factor_,
                     global_localization_non_free_space_factor_,
                     non_free_space_radius_);
  }
}

void
AmclNode::freeMapDependentMemory3D()
{
  delete lidar_;
  lidar_ = NULL;
}

void
AmclNode::deleteAmclNode3D()
{
  delete lidar_scan_filter_;
  delete lidar_scan_sub_;
}
