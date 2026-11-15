#ifndef slic3r_ModelCommon_hpp_
#define slic3r_ModelCommon_hpp_

#include "ObjectID.hpp"
#include "PrintConfig.hpp"
#include "TriangleSelector.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace cereal {
	class BinaryInputArchive;
	class BinaryOutputArchive;
	template <class T> void load_optional(BinaryInputArchive &ar, std::shared_ptr<const T> &ptr);
	template <class T> void save_optional(BinaryOutputArchive &ar, const std::shared_ptr<const T> &ptr);
	template <class T> void load_by_value(BinaryInputArchive &ar, T &obj);
	template <class T> void save_by_value(BinaryOutputArchive &ar, const T &obj);
}

namespace Slic3r {
enum class ConversionType;

class BuildVolume;
class Model;
class ModelInstance;
class ModelMaterial;
class ModelObject;
class ModelVolume;
class ModelWipeTower;
class Print;
class SLAPrint;
class TriangleSelector;
//BBS: add Preset
class Preset;
class BBLProject;

class KeyStore;

namespace UndoRedo {
	class StackImpl;
}

namespace Internal {
	template<typename T>
	class StaticSerializationWrapper
	{
	public:
		StaticSerializationWrapper(T &wrap) : wrapped(wrap) {}
	private:
		friend class cereal::access;
		friend class UndoRedo::StackImpl;
		template<class Archive> void load(Archive &ar) { cereal::load_by_value(ar, wrapped); }
		template<class Archive> void save(Archive &ar) const { cereal::save_by_value(ar, wrapped); }
		T&	wrapped;
	};
}

typedef std::string t_model_material_id;
typedef std::string t_model_material_attribute;
typedef std::map<t_model_material_attribute, std::string> t_model_material_attributes;

typedef std::map<t_model_material_id, ModelMaterial*> ModelMaterialMap;
typedef std::vector<ModelObject*> ModelObjectPtrs;
typedef std::vector<ModelVolume*> ModelVolumePtrs;
typedef std::vector<ModelInstance*> ModelInstancePtrs;


class LayerHeightProfile final : public ObjectWithTimestamp {
public:
    // Assign the content if the timestamp differs, don't assign an ObjectID.
    void assign(const LayerHeightProfile &rhs) { if (! this->timestamp_matches(rhs)) { m_data = rhs.m_data; this->copy_timestamp(rhs); } }
    void assign(LayerHeightProfile &&rhs) { if (! this->timestamp_matches(rhs)) { m_data = std::move(rhs.m_data); this->copy_timestamp(rhs); } }

    std::vector<coordf_t> get() const throw() { return m_data; }
    bool                  empty() const throw() { return m_data.empty(); }
    void                  set(const std::vector<coordf_t> &data) { if (m_data != data) { m_data = data; this->touch(); } }
    void                  set(std::vector<coordf_t> &&data) { if (m_data != data) { m_data = std::move(data); this->touch(); } }
    void                  clear() { m_data.clear(); this->touch(); }

    template<class Archive> void serialize(Archive &ar)
    {
        ar(cereal::base_class<ObjectWithTimestamp>(this), m_data);
    }

private:
    // Constructors to be only called by derived classes.
    // Default constructor to assign a unique ID.
    explicit LayerHeightProfile() = default;
    // Constructor with ignored int parameter to assign an invalid ID, to be replaced
    // by an existing ID copied from elsewhere.
    explicit LayerHeightProfile(int) : ObjectWithTimestamp(-1) {}
    // Copy constructor copies the ID.
    explicit LayerHeightProfile(const LayerHeightProfile &rhs) = default;
    // Move constructor copies the ID.
    explicit LayerHeightProfile(LayerHeightProfile &&rhs) = default;

    // called by ModelObject::assign_copy()
    LayerHeightProfile& operator=(const LayerHeightProfile &rhs) = default;
    LayerHeightProfile& operator=(LayerHeightProfile &&rhs) = default;

    std::vector<coordf_t> m_data;

    // to access set_new_unique_id() when copy / pasting an object
    friend class ModelObject;
};

enum class CutMode : int {
    cutPlanar,
    cutTongueAndGroove
};

enum class CutConnectorType : int {
    Plug
    , Dowel
    , Snap
    , Undef
};

enum class CutConnectorStyle : int {
    Prism
    , Frustum
    , Undef
    //,Claw
};

enum class CutConnectorShape : int {
    Triangle
    , Square
    , Hexagon
    , Circle
    , Undef
    //,D-shape
};
struct CutConnectorParas
{
    float snap_space_proportion{0.3};
    float snap_bulge_proportion{0.15};
};

struct CutConnectorAttributes
{
    CutConnectorType    type{ CutConnectorType::Plug };
    CutConnectorStyle   style{ CutConnectorStyle::Prism };
    CutConnectorShape   shape{ CutConnectorShape::Circle };

    CutConnectorAttributes() {}

    CutConnectorAttributes(CutConnectorType t, CutConnectorStyle st, CutConnectorShape sh)
        : type(t), style(st), shape(sh)
    {}

    CutConnectorAttributes(const CutConnectorAttributes& rhs) :
        CutConnectorAttributes(rhs.type, rhs.style, rhs.shape) {}

    bool operator==(const CutConnectorAttributes& other) const;

    bool operator!=(const CutConnectorAttributes& other) const { return !(other == (*this)); }

    bool operator<(const CutConnectorAttributes& other) const {
        return   this->type <  other.type ||
                (this->type == other.type && this->style <  other.style) ||
                (this->type == other.type && this->style == other.style && this->shape < other.shape);
    }

    template<class Archive> inline void serialize(Archive& ar) {
        ar(type, style, shape);
    }
};

struct CutConnector
{
    Vec3d pos;
    Transform3d rotation_m;
    float radius;
    float height;
    float radius_tolerance;// [0.f : 1.f]
    float height_tolerance;// [0.f : 1.f]
    float z_angle {0.f};
    CutConnectorAttributes attribs;

    CutConnector()
        : pos(Vec3d::Zero()), rotation_m(Transform3d::Identity()), radius(5.f), height(10.f), radius_tolerance(0.f), height_tolerance(0.1f), z_angle(0.f)
    {}

    CutConnector(Vec3d p, Transform3d rot, float r, float h, float rt, float ht, float za, CutConnectorAttributes attributes)
        : pos(p), rotation_m(rot), radius(r), height(h), radius_tolerance(rt), height_tolerance(ht), z_angle(za), attribs(attributes)
    {}

    CutConnector(const CutConnector& rhs) :
        CutConnector(rhs.pos, rhs.rotation_m, rhs.radius, rhs.height, rhs.radius_tolerance, rhs.height_tolerance, rhs.z_angle, rhs.attribs) {}

    bool operator==(const CutConnector& other) const;

    bool operator!=(const CutConnector& other) const { return !(other == (*this)); }

    template<class Archive> inline void serialize(Archive& ar) {
        ar(pos, rotation_m, radius, height, radius_tolerance, height_tolerance, z_angle, attribs);
    }
};

using CutConnectors = std::vector<CutConnector>;

// Declared outside of ModelVolume, so it could be forward declared.
enum class ModelVolumeType : int {
    INVALID = -1,
    MODEL_PART = 0,
    NEGATIVE_VOLUME,
    PARAMETER_MODIFIER,
    SUPPORT_BLOCKER,
    SUPPORT_ENFORCER,
};

//enum class EnforcerBlockerType : int8_t {
//    // Maximum is 3. The value is serialized in TriangleSelector into 2 bits.
//    NONE      = 0,
//    ENFORCER  = 1,
//    BLOCKER   = 2,
//    // Maximum is 15. The value is serialized in TriangleSelector into 6 bits using a 2 bit prefix code.
//    Extruder1 = ENFORCER,
//    Extruder2 = BLOCKER,
//    Extruder3,
//    Extruder4,
//    Extruder5,
//    Extruder6,
//    Extruder7,
//    Extruder8,
//    Extruder9,
//    Extruder10,
//    Extruder11,
//    Extruder12,
//    Extruder13,
//    Extruder14,
//    Extruder15,
//    Extruder16,
//    ExtruderMax = Extruder16
//};

enum class ConversionType : int {
    CONV_TO_INCH,
    CONV_FROM_INCH,
    CONV_TO_METER,
    CONV_FROM_METER,
};


enum ModelInstanceEPrintVolumeState : unsigned char
{
    ModelInstancePVS_Inside,
    ModelInstancePVS_Partly_Outside,
    ModelInstancePVS_Fully_Outside,
    ModelInstanceNum_BedStates
};

// BBS structure stores extruder parameters and speed map of all models
struct ExtruderParams
{
    std::string materialName;
    //std::array<double, BedType::btCount> bedTemp;
    int bedTemp;
    double heatEndTemp;
    // Parameterized filament behavior (populated from filament config fields)
    double thermalLength      = 200.0;
    double brimAdhesionCoeff  = 1.0;
    double bedAdhesionStrength = 0.02;
};

struct GlobalSpeedMap
{
    double perimeterSpeed;
    double externalPerimeterSpeed;
    double infillSpeed;
    double solidInfillSpeed;
    double topSolidInfillSpeed;
    double supportSpeed;
    double smallPerimeterSpeed;
    double maxSpeed;
    Polygon bed_poly;
};

/* Profile data */
class ModelProfileInfo
{
public:
    std::string ProfileTile;
    std::string ProfileCover;
    std::string ProfileDescription;
    std::string ProfileUserId;
    std::string ProfileUserName;
};

/* info in ModelDesignInfo can not changed after initialization */
class ModelDesignInfo
{
public:
    std::string DesignId;               // DisignId for Model
    std::string Designer;               // Designer nickname in utf8
    std::string DesignerUserId;         // Designer user_id string
};

/* info in ModelInfo can be changed after initialization */
class ModelInfo
{
public:
    std::string cover_file;     // utf8 format
    std::string license;        // utf8 format
    std::string description;    // utf8 format
    std::string copyright;      // utf8 format
    std::string model_name;     // utf8 format
    std::string origin;         // utf8 format

    std::map<std::string, std::string> metadata_items; // other meta data items

    void load(ModelInfo &info) {
        this->cover_file    = info.cover_file;
        this->license       = info.license;
        this->description   = info.description;
        this->copyright     = info.copyright;
        this->model_name    = info.model_name;
        this->origin        = info.origin;
        this->metadata_items = info.metadata_items;
    }
};

class ModelConfigObject : public ObjectBase, public ModelConfig
{
private:
	friend class cereal::access;
	friend class UndoRedo::StackImpl;
	friend class ModelObject;
	friend class ModelVolume;
	friend class ModelMaterial;

    // Constructors to be only called by derived classes.
    // Default constructor to assign a unique ID.
    explicit ModelConfigObject() = default;
    // Constructor with ignored int parameter to assign an invalid ID, to be replaced
    // by an existing ID copied from elsewhere.
    explicit ModelConfigObject(int) : ObjectBase(-1) {}
    // Copy constructor copies the ID.
	explicit ModelConfigObject(const ModelConfigObject &cfg) = default;
    // Move constructor copies the ID.
	explicit ModelConfigObject(ModelConfigObject &&cfg) = default;

    Timestamp          timestamp() const throw() override { return this->ModelConfig::timestamp(); }
    bool               object_id_and_timestamp_match(const ModelConfigObject &rhs) const throw() { return this->id() == rhs.id() && this->timestamp() == rhs.timestamp(); }

    // called by ModelObject::assign_copy()
	ModelConfigObject& operator=(const ModelConfigObject &rhs) = default;
    ModelConfigObject& operator=(ModelConfigObject &&rhs) = default;

    template<class Archive> void serialize(Archive &ar) {
        ar(cereal::base_class<ModelConfig>(this));
    }
};

class Model;
// Material, which may be shared across multiple ModelObjects of a single Model.
class ModelMaterial final : public ObjectBase
{
public:
    // Attributes are defined by the AMF file format, but they don't seem to be used by Slic3r for any purpose.
    t_model_material_attributes attributes;
    // Dynamic configuration storage for the object specific configuration values, overriding the global configuration.
    ModelConfigObject config;

    Model* get_model() const { return m_model; }
    void apply(const t_model_material_attributes &attributes)
        { this->attributes.insert(attributes.begin(), attributes.end()); }

private:
    // Parent, owning this material.
    Model *m_model;

    // To be accessed by the Model.
    friend class Model;
	// Constructor, which assigns a new unique ID to the material and to its config.
	ModelMaterial(Model *model) : m_model(model) { assert(this->id().valid()); }
	// Copy constructor copies the IDs of the ModelMaterial and its config, and m_model!
	ModelMaterial(const ModelMaterial &rhs) = default;
	void set_model(Model *model) { m_model = model; }
	void set_new_unique_id() { ObjectBase::set_new_unique_id(); this->config.set_new_unique_id(); }

	// To be accessed by the serialization and Undo/Redo code.
	friend class cereal::access;
	friend class UndoRedo::StackImpl;
	// Create an object for deserialization, don't allocate IDs for ModelMaterial and its config.
	ModelMaterial() : ObjectBase(-1), config(-1), m_model(nullptr) { assert(this->id().invalid()); assert(this->config.id().invalid()); }
	template<class Archive> void serialize(Archive &ar) {
		assert(this->id().invalid()); assert(this->config.id().invalid());
		Internal::StaticSerializationWrapper<ModelConfigObject> config_wrapper(config);
		ar(attributes, config_wrapper);
		// assert(this->id().valid()); assert(this->config.id().valid());
	}

	// Disabled methods.
	ModelMaterial(ModelMaterial &&rhs) = delete;
	ModelMaterial& operator=(const ModelMaterial &rhs) = delete;
    ModelMaterial& operator=(ModelMaterial &&rhs) = delete;
};

};

#endif /* slic3r_ModelCommon_hpp_ */
