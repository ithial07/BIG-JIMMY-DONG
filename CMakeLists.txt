cmake_minimum_required(VERSION 3.21)
project(ArdaBestClient VERSION 0.36.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
qt_standard_project_setup()

qt_add_executable(ardabest_client
    src/main.cpp
)

qt_add_executable(ardabest_client_safe
    src/main_safe.cpp
)

qt_add_resources(ardabest_client "app_resources"
    PREFIX "/"
    FILES
        resources/ardabest.json

        resources/icons/angry_orc_warrior_avatar_icon.png
        resources/icons/menacing_orc_warrior_emblem.png
        resources/icons/old_goblin_rogue_portrait.png
        resources/icons/menacing_stone_skinned_ogre_icon.png
        resources/icons/fierce_wolf_emblem_with_glowing_eyes.png
        resources/icons/regal_elf_with_ornate_tiara.png
        resources/icons/dwarf_warrior_portrait_with_braided_beard.png
        resources/icons/skull_and_crossed_swords_emblem.png
        resources/icons/butcher_s_pride_emblem.png
        resources/icons/medieval_armor_emblem_with_shield.png
        resources/icons/blacksmith_forge_emblem_icon.png
        resources/icons/merchant_s_crate_and_supplies_emblem.png
        resources/icons/medieval_fortress_gate_emblem.png
        resources/icons/golden_trophy_with_laurel_wreath.png
        resources/icons/undead_warrior_emblem_with_battered_helm.png
        resources/icons/rogue_assassin_emblem_design.png
        resources/icons/medieval_tavern_beer_mug_emblem.png
        resources/icons/herbal_alchemy_emblem_with_potion.png
        resources/icons/tailor_s_craftsmanship_emblem.png
        resources/icons/cave_entrance_badge_with_torchlight.png
        resources/icons/decorative_cobblestone_tile_icon.png
)


qt_add_resources(ardabest_client_safe "safe_resources"
    PREFIX "/"
    FILES
        resources/ardabest.json
)

target_link_libraries(ardabest_client PRIVATE Qt6::Widgets Qt6::Network)
target_link_libraries(ardabest_client_safe PRIVATE Qt6::Widgets Qt6::Network)

if(WIN32)
    set_target_properties(ardabest_client PROPERTIES WIN32_EXECUTABLE TRUE)
    set_target_properties(ardabest_client_safe PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

install(TARGETS ardabest_client ardabest_client_safe RUNTIME DESTINATION . BUNDLE DESTINATION .)
