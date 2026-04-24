add_rules("mode.debug", "mode.release")

add_requires("nlohmann_json v3.11.3")
-- Tell Xmake to fetch Dobby automatically
add_requires("dobby") 

target("ForceCloseOreUI")
    set_kind("shared")
    add_files("src/**.cpp")
    add_includedirs("src")
    
    set_languages("c++20")
    set_strip("all")
    
    add_packages("nlohmann_json")
    
    if is_plat("android") then
        remove_files("src/api/memory/win/**.cpp", "src/api/memory/win/**.h")
        add_cxflags("-O3")
        
        -- Link Dobby and Android's logging library to the build
        add_packages("dobby") 
        add_syslinks("log")
end
