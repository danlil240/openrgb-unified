/*---------------------------------------------------------*\
||| PresetRegistry.cpp                                        |
|||                                                           |
|||   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "PresetRegistry.h"

#include <filesystem>
#include <functional>
#include <set>

namespace studio
{

void PresetRegistry::SetDefaults(std::vector<DevicePreset> d)
{
    defaults.clear();
    for(DevicePreset& p : d)
    {
        defaults[p.id] = p;
    }
}

void PresetRegistry::ClearFiles()
{
    types.clear();
    file_errors.clear();
}

const DevicePreset* PresetRegistry::Find(const std::string& id) const
{
    const auto it = types.find(id);
    if(it != types.end())
    {
        return &it->second;
    }
    const auto dit = defaults.find(id);
    if(dit != defaults.end())
    {
        return &dit->second;
    }
    return nullptr;
}

std::vector<std::string> PresetRegistry::Ids() const
{
    std::vector<std::string> ids;
    for(const auto& kv : defaults)
    {
        ids.push_back(kv.first);
    }
    for(const auto& kv : types)
    {
        if(defaults.find(kv.first) == defaults.end())
        {
            ids.push_back(kv.first);
        }
    }
    return ids;
}

static std::string BasenameType(const std::string& path)
{
    const std::string name =
        std::filesystem::path(path).filename().string();
    const std::string suffix = ".device.json";
    if(name.size() > suffix.size()
       && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
    {
        return name.substr(0, name.size() - suffix.size());
    }
    return std::string();
}

bool PresetRegistry::LoadDirectory(const std::string& dir,
                                   std::vector<std::string>* errors)
{
    bool ok = true;
    std::error_code ec;
    if(!std::filesystem::is_directory(dir, ec))
    {
        return true;   /* no preset dir = only defaults; not an error */
    }
    for(const auto& entry : std::filesystem::directory_iterator(dir, ec))
    {
        if(!entry.is_regular_file())
        {
            continue;
        }
        const std::string path = entry.path().string();
        if(BasenameType(path).empty())
        {
            continue;
        }
        DevicePreset p;
        std::vector<std::string> perrs;
        if(!DevicePresetFromJsonFile(path, p, &perrs))
        {
            ok = false;
            const std::string msg = perrs.empty() ? "invalid" : perrs.front();
            file_errors[path] = msg;
            if(errors)
            {
                for(const std::string& e : perrs)
                {
                    errors->push_back(e);
                }
            }
            continue;
        }
        const std::string base = BasenameType(path);
        if(p.id != base)
        {
            /* id must match the file name — never silently pick
               whichever file was found first. */
            ok = false;
            const std::string msg = "id '" + p.id + "' does not match"
                                    " file name '" + base + "'";
            file_errors[path] = msg;
            if(errors)
            {
                errors->push_back(path + ": " + msg);
            }
            continue;
        }
        types[p.id] = p;
        file_errors.erase(path);
    }
    /* Dependency validation can only run once the whole directory
       is in — a type may reference a sibling loaded later. */
    std::vector<std::string> dep_errs;
    if(!ValidateDependencies(&dep_errs))
    {
        ok = false;
        if(errors)
        {
            for(const std::string& e : dep_errs)
            {
                errors->push_back(e);
            }
        }
    }
    return ok;
}

bool PresetRegistry::LoadFile(const std::string& path,
                              std::vector<std::string>* errors)
{
    DevicePreset p;
    if(!DevicePresetFromJsonFile(path, p, errors))
    {
        return false;
    }
    const std::string base = BasenameType(path);
    if(!base.empty() && p.id != base)
    {
        if(errors)
        {
            errors->push_back(path + ": id '" + p.id
                              + "' does not match file name '" + base + "'");
        }
        return false;
    }
    const auto backup = types;
    types[p.id] = p;
    file_errors.erase(path);
    std::vector<std::string> dep_errs;
    if(!ValidateDependencies(&dep_errs))
    {
        types = backup;   /* leave the registry unchanged */
        if(errors)
        {
            for(const std::string& e : dep_errs)
            {
                errors->push_back(e);
            }
        }
        return false;
    }
    return true;
}

bool PresetRegistry::Add(const DevicePreset& p,
                         std::vector<std::string>* errors)
{
    const auto backup = types;
    types[p.id] = p;
    if(!ValidateDependencies(errors))
    {
        types = backup;
        return false;
    }
    return true;
}

bool PresetRegistry::ValidateDependencies(std::vector<std::string>* errors)
{
    bool ok = true;

    /* DFS over the file layer. state: 0 unvisited, 1 on stack,
       2 clean, -1 bad. A type is bad when a dependency is missing
       from BOTH layers, is bad itself, or a back edge closes a
       reference cycle. Bad types are removed and the pass repeats —
       removal cascades to types that depended on them. */
    for(;;)
    {
        std::map<std::string, int> state;
        std::set<std::string>       bad;
        std::vector<std::string>    stack;

        std::function<bool(const std::string&)> visit =
            [&](const std::string& id) -> bool
        {
            const auto sit = state.find(id);
            if(sit != state.end())
            {
                if(sit->second == 1)
                {
                    /* Back edge — everything on the stack from `id`
                       onward closes the cycle. */
                    for(size_t i = 0; i < stack.size(); i++)
                    {
                        if(stack[i] == id)
                        {
                            for(size_t k = i; k < stack.size(); k++)
                            {
                                bad.insert(stack[k]);
                            }
                            break;
                        }
                    }
                    return false;
                }
                return sit->second == 2;
            }
            const DevicePreset* p = Find(id);
            if(p == nullptr)
            {
                return false;
            }
            state[id] = 1;
            stack.push_back(id);
            bool clean = true;
            for(const std::string& dep : PresetDependencies(*p))
            {
                if(Find(dep) == nullptr)
                {
                    clean = false;
                    if(errors)
                    {
                        errors->push_back("type '" + id
                            + "': missing dependency '" + dep + "'");
                    }
                    continue;
                }
                if(!visit(dep))
                {
                    clean = false;
                    if(state[dep] == 1)
                    {
                        if(errors)
                        {
                            errors->push_back("type '" + id
                                + "': dependency cycle via '" + dep + "'");
                        }
                    }
                }
            }
            stack.pop_back();
            state[id] = clean ? 2 : -1;
            if(!clean)
            {
                bad.insert(id);
            }
            return clean;
        };

        for(const auto& kv : types)
        {
            visit(kv.first);
        }

        if(bad.empty())
        {
            break;
        }
        ok = false;
        for(const std::string& id : bad)
        {
            if(errors)
            {
                errors->push_back("type '" + id
                    + "' removed: unresolvable or cyclic dependencies");
            }
            types.erase(id);
        }
    }
    return ok;
}

} /* namespace studio */
