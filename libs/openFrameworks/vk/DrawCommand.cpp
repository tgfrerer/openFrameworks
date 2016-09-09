#include "vk/DrawCommand.h"
#include "vk/RenderBatch.h"

// setup all non-transient state for this draw object

of::DrawCommand::DrawCommand( const DrawCommandInfo & dcs )
	:mDrawCommandInfo( dcs ){
	auto state = ( const_cast<DrawCommandInfo&>( mDrawCommandInfo ) );

	// at this point we can find out how many descriptors 
	// this draw operation will need, and we can store 
	// them with the draw operation.

	//! Todo: initialise Ubo blobs with default values, based on 
	//  default values received from Shader. 
	//
	// Shader should provide us with values to initialise, because
	// these values depend on the shader, and the shader knows the
	// uniform variable types.
}


