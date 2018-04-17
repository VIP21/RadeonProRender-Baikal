#include "material_io.h"

#include "SceneGraph/scene1.h"
#include "SceneGraph/iterator.h"
#include "SceneGraph/shape.h"
#include "SceneGraph/material.h"
#include "SceneGraph/Collector/collector.h"
#include "SceneGraph/uberv2material.h"

#include "image_io.h"

#include "XML/tinyxml2.h"

#include <sstream>
#include <map>
#include <stack>
#include <string>
#include <assert.h>

namespace Baikal
{
    using namespace tinyxml2;

    // XML based material IO implememtation
    class MaterialIoXML : public MaterialIo
    {
    public:
        // Save materials to disk
        void SaveMaterials(std::string const& filename, Iterator& iterator) override;

        // Load materials from disk
        std::unique_ptr<Iterator> LoadMaterials(std::string const& file_name) override;

    private:
        // Write single material
        void WriteMaterial(ImageIo& io, XMLPrinter& printer, Material::Ptr material);
        // Write single material input
        void WriteInput(ImageIo& io, XMLPrinter& printer, std::string const& name, Material::InputValue value);
        // Load single material
        Material::Ptr LoadMaterial(ImageIo& io, XMLElement& element);
        // Load single input
        void LoadInput(ImageIo& io, Material::Ptr material, XMLElement& element);

        // Texture to name map
        std::map<Texture::Ptr, std::string> m_tex2name;

        std::map<std::string, Texture::Ptr> m_name2tex;
        std::map<std::uint64_t, Material::Ptr> m_id2mat;

        struct ResolveRequest
        {
            Material::Ptr material;
            std::string input;
            std::uint64_t id;

            bool operator < (ResolveRequest const& rhs) const
            {
                return id < rhs.id;
            }
        };

        std::set<ResolveRequest> m_resolve_requests;
        std::string m_base_path;
    };

    std::unique_ptr<MaterialIo> MaterialIo::CreateMaterialIoXML()
    {
        return std::make_unique<MaterialIoXML>();
    }

    static std::string Float4ToString(RadeonRays::float3 const& v)
    {
        std::ostringstream oss;
        oss << v.x << " " <<  v.y << " " << v.z << " " << v.w;
        return oss.str();
    }

    void MaterialIoXML::WriteInput(ImageIo& io, XMLPrinter& printer, std::string const& name, Material::InputValue value)
    {
        printer.OpenElement("Input");

        printer.PushAttribute("name", name.c_str());

        if (value.type == Material::InputType::kFloat4)
        {
            printer.PushAttribute("type", "float4");
            printer.PushAttribute("value", Float4ToString(value.float_value).c_str());
        }
        else if (value.type == Material::InputType::kTexture)
        {
            printer.PushAttribute("type", "texture");

            auto iter = m_tex2name.find(value.tex_value);

            if (iter != m_tex2name.cend())
            {
                printer.PushAttribute("value", iter->second.c_str());
            }
            else
            {
                std::ostringstream oss;
                oss << (std::uint64_t)value.tex_value.get() << ".jpg";

                io.SaveImage(m_base_path + oss.str(), value.tex_value);

                m_tex2name[value.tex_value] = oss.str();

                printer.PushAttribute("value", oss.str().c_str());
            }
        }
        else
        {
            printer.PushAttribute("type", "material");

            printer.PushAttribute("value", (int)(reinterpret_cast<std::uint64_t>(value.mat_value.get())));
        }

        printer.CloseElement();
    }

    void MaterialIoXML::WriteMaterial(ImageIo& io, XMLPrinter& printer, Material::Ptr material)
    {
        printer.OpenElement("Material");

        printer.PushAttribute("name", material->GetName().c_str());

        printer.PushAttribute("id", (int)(reinterpret_cast<std::uint64_t>(material.get())));

        printer.PushAttribute("thin", material->IsThin());

        auto uberv2_material = std::dynamic_pointer_cast<UberV2Material>(material);

        if (uberv2_material)
        {
            //Write properties
            printer.PushAttribute("refraction_link_ior", uberv2_material->IsLinkRefractionIOR());
            printer.PushAttribute("emission_doublesided", uberv2_material->isDoubleSided());
            printer.PushAttribute("sss_multyscatter", uberv2_material->IsMultiscatter());
            printer.PushAttribute("layers", uberv2_material->GetLayers());


            SingleBxdf::BxdfType type = bxdf->GetBxdfType();

            printer.PushAttribute("bxdf", BxdfToString(type).c_str());

            SingleBxdf::InputValue albedo = bxdf->GetInputValue("albedo");

            WriteInput(io, printer,"albedo", albedo);

            SingleBxdf::InputValue normal = bxdf->GetInputValue("normal");

            if (normal.tex_value)
            {
                WriteInput(io, printer, "normal", normal);
            }
            else
            {
                SingleBxdf::InputValue bump = bxdf->GetInputValue("bump");

                if (bump.tex_value)
                {
                    WriteInput(io, printer, "bump", bump);
                }
            }

            SingleBxdf::InputValue ior = bxdf->GetInputValue("ior");

            WriteInput(io, printer,"ior", ior);

            SingleBxdf::InputValue fresnel = bxdf->GetInputValue("fresnel");

            WriteInput(io, printer,"fresnel", fresnel);

            if (type == SingleBxdf::BxdfType::kMicrofacetGGX ||
                type == SingleBxdf::BxdfType::kMicrofacetBeckmann ||
                type == SingleBxdf::BxdfType::kMicrofacetRefractionGGX ||
                type == SingleBxdf::BxdfType::kMicrofacetRefractionBeckmann)
            {
                SingleBxdf::InputValue roughness = bxdf->GetInputValue("roughness");
                WriteInput(io, printer,"roughness", roughness);
            }
        }
        else
        {
            assert(!"Only UberV2 materials supported")
        }

        printer.CloseElement();
    }

    void MaterialIoXML::SaveMaterials(std::string const& filename, Iterator& mat_iter)
    {
        auto slash = filename.find_last_of('/');
        if (slash == std::string::npos) slash = filename.find_last_of('\\');
        if (slash != std::string::npos)
            m_base_path.assign(filename.cbegin(), filename.cbegin() + slash + 1);
        else
            m_base_path.clear();

        XMLDocument doc;
        XMLPrinter printer;

        m_tex2name.clear();

        auto image_io = ImageIo::CreateImageIo();

        for (mat_iter.Reset();mat_iter.IsValid(); mat_iter.Next())
        {
            auto material = mat_iter.ItemAs<Material>();

            if (material)
            {
                WriteMaterial(*image_io, printer, material);
            }
        }

        doc.Parse(printer.CStr());

        doc.SaveFile(filename.c_str());
    }

    void MaterialIoXML::LoadInput(ImageIo& io, Material::Ptr material, XMLElement& element)
    {
        std::string type(element.Attribute("type"));
        std::string name(element.Attribute("name"));

        if (type == "float4")
        {
            std::istringstream iss(element.Attribute("value"));

            RadeonRays::float4 value;

            iss >> value.x >> value.y >> value.z >> value.w;

            material->SetInputValue(name, value);

        }
        else if (type == "texture")
        {
            std::string filename(element.Attribute("value"));

            auto iter = m_name2tex.find(filename);

            if (iter != m_name2tex.cend())
            {
                material->SetInputValue(name, iter->second);
            }
            else
            {
                auto texture = io.LoadImage(m_base_path + filename);
                material->SetInputValue(name, texture);
                m_name2tex[name] = texture;
            }
        }
        else if (type == "material")
        {
            auto id = static_cast<std::uint64_t>(std::atoi(element.Attribute("value")));

            auto iter = m_id2mat.find(id);

            if (iter != m_id2mat.cend())
            {
                material->SetInputValue(name, iter->second);
            }
            else
            {
                m_resolve_requests.emplace(ResolveRequest{ material, name, id });
            }
        }
        else
        {
            throw std::runtime_error("Unsupported input type");
        }
    }

    Material::Ptr MaterialIoXML::LoadMaterial(ImageIo& io, XMLElement& element)
    {
        std::string name(element.Attribute("name"));
        std::string type(element.Attribute("type"));

        auto attribute_thin = element.Attribute("thin");
        std::string thin(attribute_thin ? attribute_thin : "");
        auto id = static_cast<std::uint64_t>(std::atoi(element.Attribute("id")));

        Material::Ptr material = nullptr;

        if (type == "simple")
        {
            auto bxdf = SingleBxdf::Create(SingleBxdf::BxdfType::kLambert);

            auto bxdf_type = StringToBxdf(element.Attribute("bxdf"));

            bxdf->SetBxdfType(bxdf_type);

            material = bxdf;
        }
        else if (type == "blend")
        {
            auto blend = MultiBxdf::Create(MultiBxdf::Type::kFresnelBlend);

            auto blend_type = static_cast<MultiBxdf::Type>(std::atoi(element.Attribute("blend_type")));

            blend->SetType(blend_type);

            material = blend;
        }
        else
        {
            throw std::runtime_error("Unsupported material type");
        }

        material->SetName(name);

        material->SetThin(thin == "true" ? true : false);

        for (auto input = element.FirstChildElement(); input; input = input->NextSiblingElement())
        {
            LoadInput(io, material, *input);
        }

        m_id2mat[id] = material;

        return material;
    }

    std::unique_ptr<Iterator> MaterialIoXML::LoadMaterials(std::string const& file_name)
    {
        m_id2mat.clear();
        m_name2tex.clear();
        m_resolve_requests.clear();

        auto slash = file_name.find_last_of('/');
        if (slash == std::string::npos) slash = file_name.find_last_of('\\');
        if (slash != std::string::npos)
            m_base_path.assign(file_name.cbegin(), file_name.cbegin() + slash + 1);
        else
            m_base_path.clear();

        XMLDocument doc;
        doc.LoadFile(file_name.c_str());

        auto image_io = ImageIo::CreateImageIo();

        std::set<Material::Ptr> materials;
        for (auto element = doc.FirstChildElement(); element; element = element->NextSiblingElement())
        {
            auto material = LoadMaterial(*image_io, *element);
            materials.insert(material);
        }

        // Fix up non-resolved stuff
        for (auto& i : m_resolve_requests)
        {
            i.material->SetInputValue(i.input, m_id2mat[i.id]);
        }

        return std::make_unique<ContainerIterator<std::set<Material::Ptr>>>(std::move(materials));
    }

    void MaterialIo::SaveMaterialsFromScene(std::string const& filename, Scene1 const& scene)
    {
        auto shape_iter = scene.CreateShapeIterator();

        Collector mat_collector;
        // Collect materials from shapes first
        mat_collector.Collect(*shape_iter,
            // This function adds all materials to resulting map
            // recursively via Material dependency API
        [](SceneObject::Ptr item) -> std::set<SceneObject::Ptr>
        {
            // Resulting material set
            std::set<SceneObject::Ptr> mats;
            // Material stack
            std::stack<Material::Ptr> material_stack;

            // Get material from current shape
            auto shape = std::static_pointer_cast<Shape>(item);
            auto material = shape->GetMaterial();

            if (material)
            {
                // Push to stack as an initializer
                material_stack.push(material);
            }

            // Drain the stack
            while (!material_stack.empty())
            {
                // Get current material
                auto m = material_stack.top();
                material_stack.pop();

                // Emplace into the set
                mats.emplace(m);

                // Create dependency iterator
                std::unique_ptr<Iterator> mat_iter = m->CreateMaterialIterator();

                // Push all dependencies into the stack
                for (; mat_iter->IsValid(); mat_iter->Next())
                {
                    material_stack.push(mat_iter->ItemAs<Material>());
                }
            }

            // Return resulting set
            return mats;
        });

        auto mat_iter = mat_collector.CreateIterator();

        SaveMaterials(filename, *mat_iter);
    }

    void MaterialIo::ReplaceSceneMaterials(Scene1& scene, Iterator& iterator, MaterialMap const& mapping)
    {
        std::map<std::string, Material::Ptr> name2mat;

        for (iterator.Reset(); iterator.IsValid(); iterator.Next())
        {
            auto material = iterator.ItemAs<Material>();
            auto name = material->GetName();
            name2mat[name] = material;
        }

        auto shape_iter = scene.CreateShapeIterator();

        for (; shape_iter->IsValid(); shape_iter->Next())
        {
            auto shape = shape_iter->ItemAs<Shape>();
            auto material = shape->GetMaterial();

            if (!material)
                continue;

            auto name = material->GetName();
            auto citer = mapping.find(name);

            if (citer != mapping.cend())
            {
                auto mat_iter = name2mat.find(citer->second);

                if (mat_iter != name2mat.cend())
                {
                    shape->SetMaterial(mat_iter->second);
                }
            }
        }
    }

    MaterialIo::MaterialMap MaterialIo::LoadMaterialMapping(std::string const& filename)
    {
        MaterialMap map;

        XMLDocument doc;
        doc.LoadFile(filename.c_str());

        for (auto element = doc.FirstChildElement(); element; element = element->NextSiblingElement())
        {
            std::string from(element->Attribute("from"));
            std::string to(element->Attribute("to"));
            map.emplace(from, to);
        }

        return map;
    }

    void MaterialIo::SaveIdentityMapping(std::string const& filename, Scene1 const& scene)
    {
        XMLDocument doc;
        XMLPrinter printer;

        auto shape_iter = scene.CreateShapeIterator();
        std::set<Material::Ptr> serialized_mats;

        for (; shape_iter->IsValid(); shape_iter->Next())
        {
            auto material = shape_iter->ItemAs<Shape>()->GetMaterial();

            if (material && serialized_mats.find(material) == serialized_mats.cend())
            {
                auto name = material->GetName();
                printer.OpenElement("Mapping");
                printer.PushAttribute("from", name.c_str());
                printer.PushAttribute("to", name.c_str());
                printer.CloseElement();
                serialized_mats.emplace(material);
            }
        }

        doc.Parse(printer.CStr());

        doc.SaveFile(filename.c_str());
    }
}
