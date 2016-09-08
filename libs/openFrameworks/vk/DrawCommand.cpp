#include "vk/DrawCommand.h"
#include "vk/RenderBatch.h"

// setup all non-transient state for this draw object

of::DrawCommand::DrawCommand( const DrawCommandInfo & dcs )
	:mDrawCommandInfo( dcs ){
	auto state = ( const_cast<DrawCommandInfo&>( mDrawCommandInfo ) );

	// once the pipeline is inside a draw command we precalculate 
	// but this doesn't make sense when the renderpass is part
	// of pipeline code, too.

	mPipelineHash = state.getPipeline().calculateHash();

	// at this point we can find out how many descriptors 
	// this draw operation will need, and we can store 
	// them with the draw operation.

}

