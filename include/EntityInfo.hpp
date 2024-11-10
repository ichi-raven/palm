/*****************************************************************//**
 * @file   EntityInfo.hpp
 * @brief  
 * 
 * @author ichi-raven
 * @date   October 2024
 *********************************************************************/
#ifndef PALM_INCLUDE_ENTITYINFO_HPP_
#define PALM_INCLUDE_ENTITYINFO_HPP_

namespace palm
{
    struct EntityInfo
    {
        std::string groupName;
        std::string entityName;
        ec2s::Entity entityID;
        bool editable = false;
    };
}

#endif
