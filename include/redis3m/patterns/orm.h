#pragma once

#include <redis3m/connection.h>
#include <map>
#include <string>
#include <algorithm>
#include <boost/foreach.hpp>
#include <redis3m/patterns/model.h>
#include <redis3m/patterns/script_exec.h>
#include <msgpack.hpp>

namespace redis3m
{
namespace patterns
{

class orm {
public:

    // Find
    template<typename Model>
    bool find_by_id(connection::ptr_t conn, const std::string& id, Model& model)
    {
        reply r = conn->run(command("HGETALL")(model_key<Model>(id)));
        if (r.elements().size() > 0 )
        {
            std::map<std::string, std::string> map;
            for (unsigned long i = 0; i < r.elements().size(); i+=2 )
            {
                map[r.elements()[i].str()] = r.elements()[i+1].str();
            }
            map["id"] = id;
            model.from_map(map);
            return true;
        }
        else
        {
            return false;
        }
    }

    template<class Model>
    bool find_by_unique_field(connection::ptr_t conn, const std::string& field, const std::string& value, Model& model)
    {
        std::string id = conn->run(command("HGET")
                           (unique_field_key<Model>(field), value)).str();
        if (!id.empty())
        {
            return find_by_id(conn, id, model);
        }
        else
        {
            return false;
        }
    }

    template<class Model>
    bool exists_by_id(connection::ptr_t conn, const std::string& id)
    {
        return conn->run(command("SISMEMBER")(collection_key<Model>(), id)).integer() == 1;
    }

    // Basic attribute handling
    template<class Model>
    std::string save(connection::ptr_t conn, const Model& model)
    {
        std::string new_id = model.id();
        // Create a new id if object is new, using redis INCR command
        if (new_id.empty())
        {
            uint64_t new_id_int = conn->run(command("INCR")(collection_id_key<Model>())).integer();
            new_id = boost::lexical_cast<std::string>(new_id_int);
        }

        std::map<std::string, std::string> model_map;
        model_map["name"] = model.model_name();
        model_map["id"] = new_id;

        std::vector<std::string> args;
        msgpack::sbuffer sbuf;  // simple buffer

        // Pack model data
        msgpack::pack(&sbuf, model_map);
        args.push_back(std::string(sbuf.data(), sbuf.size()));
        sbuf.clear();

        // pack model attributes
        std::map<std::string, std::string> attributes = model.to_map();
        std::vector<std::string> attributes_vector;
        typedef std::pair<std::string, std::string> strpair;
        BOOST_FOREACH(const strpair& item, attributes)
        {
            attributes_vector.push_back(item.first);
            attributes_vector.push_back(item.second);
        }

        msgpack::pack(&sbuf, attributes_vector);
        args.push_back(std::string(sbuf.data(), sbuf.size()));
        sbuf.clear();

        // pack model indices
        std::vector<std::pair<std::string, std::string> > indices;
        BOOST_FOREACH(const std::string& index, model.indices())
        {
            indices.push_back(std::make_pair(index, attributes[index]));
        }
        msgpack::pack(&sbuf, indices);
        args.push_back(std::string(sbuf.data(), sbuf.size()));
        sbuf.clear();

        // pack model uniques
        std::map<std::string, std::string> uniques;
        BOOST_FOREACH(const std::string& index, model.uniques())
        {
            uniques[index] = attributes[index];
        }
        msgpack::pack(&sbuf, uniques);
        args.push_back(std::string(sbuf.data(), sbuf.size()));

        reply r = save_script.exec(conn, std::vector<std::string>(), args);

        return r.str();
    }

    template<class Model>
    void remove(connection::ptr_t conn, const Model& model)
    {
        std::map<std::string, std::string> model_map;
        model_map["name"] = model.model_name();
        model_map["id"] = model.id();
        model_map["key"] = model_key<Model>(model.id());

        std::vector<std::string> args;
        msgpack::sbuffer sbuf;  // simple buffer

        // Pack model data
        msgpack::pack(&sbuf, model_map);
        args.push_back(std::string(sbuf.data(), sbuf.size()));
        sbuf.clear();

        // pack model uniques
        std::map<std::string, std::string> attributes = model.to_map();
        std::map<std::string, std::string> uniques;
        BOOST_FOREACH(const std::string& index, model.uniques())
        {
            uniques[index] = attributes[index];
        }
        msgpack::pack(&sbuf, uniques);
        args.push_back(std::string(sbuf.data(), sbuf.size()));
        sbuf.clear();

        // TODO: support tracked keys
        msgpack::pack(&sbuf, std::vector<std::string>());
        args.push_back(std::string(sbuf.data(), sbuf.size()));
        sbuf.clear();

        remove_script.exec(conn, std::vector<std::string>(), args);
    }

    template<typename Model, typename SubModel>
    std::vector<SubModel> list_members(connection::ptr_t conn, const Model& m, const std::string& list_name)
    {
        std::vector<SubModel> ret;
        reply lrange = conn->run(command("LRANGE")
                            (submodel_collection_key<Model>(m.id(), list_name))
                            ("0")("-1"));
        BOOST_FOREACH(reply r, lrange.elements())
        {
            SubModel sm;
            ret.push_back(find_by_id(conn, r.str(), sm));
        }
        return ret;
    }

//    // Subentities
//    template<class Model>
//    bool sub_model_add(const std::string& model_id, const std::string& collection, const std::string& submodel_id)
//    {
//        return sadd(submodel_collection_key<Model>(model_id, collection), submodel_id);
//    }

//    template<class Model>
//    bool sub_model_remove(const std::string& model_id, const std::string& collection, const std::string& submodel_id)
//    {
//        return srem(submodel_collection_key<Model>(model_id, collection), submodel_id);
//    }

//    template<class Model>
//    std::vector<std::string> all_subentities(const std::string& model_id, const std::string& collection)
//    {
//        return smembers(submodel_collection_key<Model>(model_id, collection));
//    }

//    template<class Model>
//    bool submodel_exists_by_id(const std::string& model_id, const std::string& collection, const std::string& submodel_id)
//    {
//        return sismember(submodel_collection_key<Model>(model_id, collection), submodel_id);
//    }

//    template<class Model>
//    void submodel_exists_by_id_throw(const std::string& model_id, const std::string& collection, const std::string& submodel_id)
//    {
//        if (! sismember(submodel_collection_key<Model>(model_id, collection), submodel_id))
//        {
//            throw Model_not_found(submodel_id + "is not on " + Model::model_name() + " " + collection);
//        }
//    }

//    template<class Model, class redis_submodel>
//    bool has_submodel_with_indexed_field(const std::string& model_id, const std::string& collection,
//                                          const std::string& submodel_indexed_field, const std::string& submodel_indexed_field_value)
//    {
//        std::vector<std::string> keys_to_intersect =
//                boost::assign::list_of
//                (submodel_collection_key<Model>(model_id, collection))
//                (indexed_field_key<redis_submodel>(submodel_indexed_field, submodel_indexed_field_value));
//        std::vector<std::string> intersection_result = sinter(keys_to_intersect);

//        return intersection_result.size() > 0;
//    }

private:

    template<class Model>
    inline std::string collection_key()
    {
        return Model::model_name() + ":all";
    }

    template<class Model>
    inline std::string collection_id_key()
    {
        return Model::model_name() + ":id";
    }

    template<typename Model>
    inline std::string model_key(const std::string& id)
    {
        return Model::model_name() + ":" + id;
    }

    template<class Model>
    inline std::string submodel_collection_key(const std::string& id, const std::string& collection_name)
    {
        return model_key<Model>(id) + ":" + collection_name;
    }

    template<class Model>
    inline std::string indexed_field_key(const std::string& field, const std::string& value)
    {
        return Model::model_name() + ":indices:" + field + ":" + value;
    }

    template<class Model>
    inline std::string unique_field_key(const std::string& field)
    {
        return Model::model_name() + ":uniques:" + field;
    }

    static script_exec save_script;
    static script_exec remove_script;
};
}
}
