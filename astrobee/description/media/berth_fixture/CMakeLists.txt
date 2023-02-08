cmake_minimum_required(VERSION 2.8.3)

project(berth_fixture)

find_package(catkin REQUIRED)

catkin_package(CATKIN_DEPENDS gazebo_ros)

find_package(roslaunch)

foreach(dir config launch meshes urdf)
	install(DIRECTORY ${dir}/
		DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}/${dir})
endforeach(dir)
