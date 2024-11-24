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
    /**
     * @brief  Struct that stores information associated with Entity
     */
    struct EntityInfo
    {
        //! Name of the group to which this Entity belongs
        std::string groupName;
        //! Name of this Entity
        std::string entityName;
        //! Keep own Entity
        ec2s::Entity entityID = ec2s::kInvalidEntity;
        //! Indicates whether this entity is editable in the editor
        bool editable = false;
    };
}

#endif
