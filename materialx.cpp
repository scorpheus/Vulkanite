#include "materialx.h"

#define MATERIALX_BUILD_SHARED_LIBS
//#include <MaterialXFormat/XmlIo.h>
#include <MaterialXFormat/Util.h>
#include <MaterialXGenShader/Util.h>
#include <MaterialXGenShader/GenContext.h>
#include <MaterialXGenShader/DefaultColorManagementSystem.h>
#include <MaterialXGenGlsl/GlslShaderGenerator.h>
//#include <MaterialXGenGlsl/GlslResourceBindingContext.h>
#include <MaterialXGenShader/shader.h>
#include <fstream>


#include <spdlog/spdlog.h>
#include <fmt/core.h>


namespace mx = MaterialX;
mx::FileSearchPath getSourceSearchPath(mx::ConstDocumentPtr doc)
{
    mx::StringSet pathSet;
    for (mx::ConstElementPtr elem : doc->traverseTree())
    {
        if (elem->hasSourceUri())
        {
            mx::FilePath sourceFilename = mx::FilePath(elem->getSourceUri());
            pathSet.insert(sourceFilename.getParentPath());
        }
    }

    mx::FileSearchPath searchPath;
    for (mx::FilePath path : pathSet)
    {
        searchPath.append(path);
    }

    return searchPath;
}

mx::FileSearchPath getDefaultDataSearchPath()
{
    const mx::FilePath REQUIRED_LIBRARY_FOLDER("libraries/targets");
    mx::FilePath currentPath = mx::FilePath::getModulePath();
    while (!currentPath.isEmpty())
    {
        if ((currentPath / REQUIRED_LIBRARY_FOLDER).exists())
        {
            return mx::FileSearchPath(currentPath);
        }
        currentPath = currentPath.getParentPath();
    }
    return mx::FileSearchPath();    
}

void generateGlsl( const std::string& filename, const std::string& outputFilename ) {
	// Initialize the standard library.
	mx::DocumentPtr _stdLib;
	mx::StringSet _xincludeFiles;
	mx::FileSearchPath _searchPath = getDefaultDataSearchPath();

	mx::FilePathVec _libraryFolders;
	// Append the standard library folder, giving it a lower precedence than user-supplied libraries.
	_libraryFolders.push_back( "libraries" );
	try {
		_stdLib = mx::createDocument();
		_xincludeFiles = mx::loadLibraries( _libraryFolders, _searchPath, _stdLib );
		if( _xincludeFiles.empty() ) {
			spdlog::debug( "Could not find standard data libraries on the given search path: {} ", _searchPath.asString() );
		}
	} catch( std::exception& e ) {
		spdlog::debug( "Failed to load standard data libraries: {}", e.what() );
		return;
	}

	try {
		mx::GenContext context( mx::GlslShaderGenerator::create() );
		// Unit registry
		mx::UnitConverterRegistryPtr _unitRegistry;

		// Initialize search path.
		context.registerSourceCodeSearchPath( _searchPath );

		// Initialize color management.
		mx::DefaultColorManagementSystemPtr cms = mx::DefaultColorManagementSystem::create( context.getShaderGenerator().getTarget() );
		cms->loadLibrary( _stdLib );
		context.getShaderGenerator().setColorManagementSystem( cms );

		// Initialize unit management.
		mx::UnitSystemPtr unitSystem = mx::UnitSystem::create( context.getShaderGenerator().getTarget() );
		unitSystem->loadLibrary( _stdLib );
		unitSystem->setUnitConverterRegistry( _unitRegistry );
		context.getShaderGenerator().setUnitSystem( unitSystem );
		context.getOptions().targetDistanceUnit = "meter";


		// Set default Glsl generator options.
		context.getOptions().targetColorSpaceOverride = "lin_rec709";
		context.getOptions().fileTextureVerticalFlip = true;
		context.getOptions().hwShadowMap = false;
	//	context.getOptions().hwImplicitBitangents = false;

		// Create an empty document.
		mx::DocumentPtr doc = mx::createDocument();
		auto _materialSearchPath = getSourceSearchPath( doc );

		// Import libraries.
		doc->importLibrary( _stdLib );

		// Load the document from a file.
		MaterialX::readFromXmlFile( doc, filename );

		// Validate the document.
		std::string message;
		if( !doc->validate( &message ) ) {
			spdlog::debug( fmt::format( "*** Validation warnings for {} ***", filename ) );
			spdlog::debug( message );
		}

		// Find new renderable elements.
		mx::StringVec renderablePaths;
		std::vector<mx::TypedElementPtr> elems;
		mx::findRenderableElements( doc, elems );
		if( elems.empty() ) {
			throw mx::Exception( "No renderable elements found in " + filename );
		}
		std::vector<mx::NodePtr> materialNodes;
		for( mx::TypedElementPtr elem : elems ) {
			mx::TypedElementPtr renderableElem = elem;
			mx::NodePtr node = elem->asA<mx::Node>();
			materialNodes.push_back( node && node->getType() == mx::MATERIAL_TYPE_STRING ? node : nullptr );
			renderablePaths.push_back( renderableElem->getNamePath() );
		}

		// Check for any udim set.
		mx::ValuePtr udimSetValue = doc->getGeomPropValue( mx::UDIM_SET_PROPERTY );

		// Create new materials.
		mx::TypedElementPtr udimElement;
		for( size_t i = 0; i < renderablePaths.size(); i++ ) {
			const auto& renderablePath = renderablePaths[i];
			mx::ElementPtr elem = doc->getDescendant( renderablePath );
			mx::TypedElementPtr typedElem = elem ? elem->asA<mx::TypedElement>() : nullptr;
			if( !typedElem ) {
				continue;
			}

			auto _hasTransparency = mx::isTransparentSurface( typedElem, context.getShaderGenerator().getTarget() );

			mx::GenContext materialContext = context;
			materialContext.getOptions().hwTransparency = _hasTransparency;

			auto _hwShader = context.getShaderGenerator().generate( "Shader", typedElem, materialContext );
			if( !_hwShader ) {
				return;
			}

			// Get the shader source code.
			const std::string& pixelShader = _hwShader->getSourceCode( mx::Stage::PIXEL );
			const std::string& vertexShader = _hwShader->getSourceCode( mx::Stage::VERTEX );

			// Write the shader code to a file.
			std::ofstream outputFile;
			outputFile.open( outputFilename + renderablePath + ".frag" );
			outputFile << pixelShader;
			outputFile.close();
			outputFile.open( outputFilename + renderablePath + ".vert" );
			outputFile << vertexShader;
			outputFile.close();
		}
	} catch( std::exception& e ) {
		spdlog::info( "Failed to load material" );
	}
}

void AssignMaterialToGeo(){
	// Apply material assignments in the order in which they are declared within the document,
	// with later assignments superseding earlier ones.
	//for( mx::LookPtr look : doc->getLooks() ) {
	//	for( mx::MaterialAssignPtr matAssign : look->getMaterialAssigns() ) {
	//		const std::string& activeGeom = matAssign->getActiveGeom();
	//		for( mx::MeshPartitionPtr part : _geometryList ) {
	//			std::string geom = part->getName();
	//			for( const std::string& id : part->getSourceNames() ) {
	//				geom += mx::ARRAY_PREFERRED_SEPARATOR + id;
	//			}
	//			if( mx::geomStringsMatch( activeGeom, geom, true ) ) {
	//				for( mx::MaterialPtr mat : newMaterials ) {
	//					if( mat->getMaterialNode() == matAssign->getReferencedMaterial() ) {
	//						assignMaterial( part, mat );
	//						break;
	//					}
	//				}
	//			}
	//			mx::CollectionPtr coll = matAssign->getCollection();
	//			if( coll && coll->matchesGeomString( geom ) ) {
	//				for( mx::MaterialPtr mat : newMaterials ) {
	//					if( mat->getMaterialNode() == matAssign->getReferencedMaterial() ) {
	//						assignMaterial( part, mat );
	//						break;
	//					}
	//				}
	//			}
	//		}
	//	}
	//}
}
//
//void GlslMaterial::bindImages(ImageHandlerPtr imageHandler, const FileSearchPath& searchPath, bool enableMipmaps)
//{
//    if (!_glProgram)
//    {
//        return;
//    }
//
//    _boundImages.clear();
//
//    const VariableBlock* publicUniforms = getPublicUniforms();
//    if (!publicUniforms)
//    {
//        return;
//    }
//    for (const auto& uniform : publicUniforms->getVariableOrder())
//    {
//        if (uniform->getType() != Type::FILENAME)
//        {
//            continue;
//        }
//        const std::string& uniformVariable = uniform->getVariable();
//        std::string filename;
//        if (uniform->getValue())
//        {
//            filename = searchPath.find(uniform->getValue()->getValueString());
//        }
//
//        // Extract out sampling properties
//        ImageSamplingProperties samplingProperties;
//        samplingProperties.setProperties(uniformVariable, *publicUniforms);
//
//        // Set the requested mipmap sampling property,
//        samplingProperties.enableMipmaps = enableMipmaps;
//
//        ImagePtr image = bindImage(filename, uniformVariable, imageHandler, samplingProperties);
//        if (image)
//        {
//            _boundImages.push_back(image);
//        }
//    }
//}
//
//ImagePtr GlslMaterial::bindImage(const FilePath& filePath, const std::string& uniformName, ImageHandlerPtr imageHandler,
//                                 const ImageSamplingProperties& samplingProperties)
//{
//    if (!_glProgram)
//    {
//        return nullptr;
//    }
//
//    // Create a filename resolver for geometric properties.
//    StringResolverPtr resolver = StringResolver::create();
//    if (!getUdim().empty())
//    {
//        resolver->setUdimString(getUdim());
//    }
//    imageHandler->setFilenameResolver(resolver);
//
//    // Acquire the given image.
//    ImagePtr image = imageHandler->acquireImage(filePath);
//    if (!image)
//    {
//        return nullptr;
//    }
//
//    // Bind the image and set its sampling properties.
//    if (imageHandler->bindImage(image, samplingProperties))
//    {
//        GLTextureHandlerPtr textureHandler = std::static_pointer_cast<GLTextureHandler>(imageHandler);
//        int textureLocation = textureHandler->getBoundTextureLocation(image->getResourceId());
//        if (textureLocation >= 0)
//        {
//            _glProgram->bindUniform(uniformName, Value::createValue(textureLocation), false);
//            return image;
//        }
//    }
//    return nullptr;
//}
