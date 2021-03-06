#include "GraphicsRenderer.h"
#include "RenderPath.h"
#include "BackEnd/vk/DeviceRenderStageVK.h"

#include "BackEnd/vk/DevicePipelineVK.h"
#include "BackEnd/vk/DeviceFrameBufferVK.h"
#include "Engine/Engine.h"
#include "BackEnd/RenderBackEndBase.h"
#include "Scene/SceneCuller.h"
#include "Technique/MaterialPool.h"
#include "3D/ShadowMap/ShadowMap.h"
#include "2D/GUISystem.h"
#include "BackEnd/vk/DeviceBufferVK.h"
#include "Rendering/Renderer.h"
#include "BackEnd/VkRenderBackEnd.h"
#include <EngineSrc\Scene\SceneMgr.h>
#include "BackEnd/vk/DeviceTextureVK.h"
#include "BackEnd/vk/DeviceShaderVK.h"
#include "3D/Thumbnail.h"
namespace tzw
{

	GraphicsRenderer::GraphicsRenderer()
	{
	}

    void GraphicsRenderer::init()
    {
        auto backEnd = static_cast<VKRenderBackEnd *>(Engine::shared()->getRenderBackEnd());

        auto thumbnailPass = new DeviceRenderPassVK(1, DeviceRenderPassVK::OpType::LOADCLEAR_AND_STORE, ImageFormat::R8G8B8A8_S, true);
        m_thumbNailRenderStage = new DeviceRenderStageVK(thumbnailPass, nullptr);

        auto size = Engine::shared()->winSize();
        auto gBufferRenderPass = new DeviceRenderPassVK(4, DeviceRenderPassVK::OpType::LOADCLEAR_AND_STORE, ImageFormat::R8G8B8A8_S, true);
        auto gBuffer = new DeviceFrameBufferVK(size.x, size.y, gBufferRenderPass);
        m_gPassStage = new DeviceRenderStageVK(gBufferRenderPass, gBuffer);


        m_shadowMat = new Material();
        m_shadowMat->loadFromTemplate("Shadow");
        m_shadowInstancedMat = new Material();
        m_shadowInstancedMat->loadFromTemplate("ShadowInstance");

        for(int i = 0; i < 3; i ++)
        {
            auto shadowRenderPass = new DeviceRenderPassVK(0, DeviceRenderPassVK::OpType::LOADCLEAR_AND_STORE, ImageFormat::R8G8B8A8_S, true);
            auto shadowBuffer = new DeviceFrameBufferVK(1024, 1024, shadowRenderPass);
            m_ShadowStage[i] = new DeviceRenderStageVK(shadowRenderPass, shadowBuffer);
        }


        auto deferredLightingPass = new DeviceRenderPassVK(1, DeviceRenderPassVK::OpType::LOADCLEAR_AND_STORE, ImageFormat::R16G16B16A16_SFLOAT, false);
        auto deferredLightingBuffer= new DeviceFrameBufferVK(size.x, size.y, deferredLightingPass);
        m_DeferredLightingStage = new DeviceRenderStageVK(deferredLightingPass, deferredLightingBuffer);
       
        
        DeviceVertexInput imguiVertexInput;
        imguiVertexInput.stride = sizeof(VertexData);
        imguiVertexInput.addVertexAttributeDesc({VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexData, m_pos)});
        imguiVertexInput.addVertexAttributeDesc({VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexData, m_color)});
        imguiVertexInput.addVertexAttributeDesc({VK_FORMAT_R32G32_SFLOAT, offsetof(VertexData, m_texCoord)});

        Material * mat = new Material();
        mat->loadFromTemplate("DirectLight");
        m_DeferredLightingStage->createSinglePipeline(mat);
        auto winSize = Engine::shared()->winSize();
        DeviceVertexInput emptyInstancingInput;

        auto skyPass = new DeviceRenderPassVK(1, DeviceRenderPassVK::OpType::LOAD_AND_STORE, ImageFormat::R16G16B16A16_SFLOAT, false);
        m_skyStage = new DeviceRenderStageVK(skyPass, m_DeferredLightingStage->getFrameBuffer());


        Material * matSkyPass = new Material();
        matSkyPass->loadFromTemplate("Sky");
        m_skyStage->createSinglePipeline(matSkyPass);

	    Material * matFog = new Material();
	    matFog->loadFromTemplate("GlobalFog");
	    MaterialPool::shared()->addMaterial("GlobalFog", matFog);
        auto fogPass = new DeviceRenderPassVK(1, DeviceRenderPassVK::OpType::LOAD_AND_STORE, ImageFormat::R16G16B16A16_SFLOAT, true);
        m_fogStage = new DeviceRenderStageVK(fogPass, m_DeferredLightingStage->getFrameBuffer());
        m_fogStage->createSinglePipeline(matFog);


        auto transparentPass = new DeviceRenderPassVK(1, DeviceRenderPassVK::OpType::LOAD_AND_STORE, ImageFormat::R16G16B16A16_SFLOAT, false);
        m_transparentStage = new DeviceRenderStageVK(transparentPass, m_DeferredLightingStage->getFrameBuffer());


        Material * matTextureToScreen = new Material();
        matTextureToScreen->loadFromTemplate("TextureToScreen");

        for(int i = 0 ; i < 2; i++)
        {
            auto pass = new DeviceRenderPassVK(1, DeviceRenderPassVK::OpType::LOADCLEAR_AND_STORE, ImageFormat::Surface_Format, false, true);
            auto frameBuffer = backEnd->createSwapChainFrameBuffer(i);//new DeviceFrameBufferVK(size.x, size.y, m_fbs[i]);
            m_textureToScreenRenderStage[i] = new DeviceRenderStageVK(pass, frameBuffer);
            m_textureToScreenRenderStage[i]->createSinglePipeline(matTextureToScreen);
        }

        for(int i = 0 ; i < 2; i++)
        {
            auto pass = new DeviceRenderPassVK(1, DeviceRenderPassVK::OpType::LOAD_AND_STORE, ImageFormat::Surface_Format, false, true);
            auto frameBuffer = backEnd->createSwapChainFrameBuffer(i);//new DeviceFrameBufferVK(size.x, size.y, m_fbs[i]);
            m_guiStage[i] = new DeviceRenderStageVK(pass, frameBuffer);
        }
	    m_imguiPipeline = nullptr;
        m_renderPath = new RenderPath();
    }

    void GraphicsRenderer::initImguiStuff()
    {
        auto backEnd = static_cast<VKRenderBackEnd *>(Engine::shared()->getRenderBackEnd());

        m_imguiIndex = static_cast<DeviceBufferVK*>(backEnd->createBuffer_imp());
        m_imguiIndex->init(DeviceBufferType::Index);
        m_imguiIndex->setAlignment(1024);
        m_imguiIndex->allocateEmpty(1024);

        m_imguiVertex = static_cast<DeviceBufferVK*>(backEnd->createBuffer_imp());
        m_imguiVertex->init(DeviceBufferType::Vertex);
        m_imguiVertex->setAlignment(1024);
        m_imguiVertex->allocateEmpty(1024);

        m_imguiMat = new Material();
        m_imguiMat->loadFromTemplate("IMGUI");


        DeviceVertexInput imguiVertexInput;
        imguiVertexInput.stride = sizeof(ImDrawVert);
        imguiVertexInput.addVertexAttributeDesc({VK_FORMAT_R32G32_SFLOAT, IM_OFFSETOF(ImDrawVert, pos)});
        imguiVertexInput.addVertexAttributeDesc({VK_FORMAT_R32G32_SFLOAT, IM_OFFSETOF(ImDrawVert, uv)});
        imguiVertexInput.addVertexAttributeDesc({VK_FORMAT_R8G8B8A8_UNORM, IM_OFFSETOF(ImDrawVert, col)});

        DeviceVertexInput instancingInput;
        vec2 winSize = Engine::shared()->winSize();
        m_imguiPipeline =  new DevicePipelineVK(winSize, m_imguiMat, backEnd->getScreenRenderPass(), imguiVertexInput, false, instancingInput);

        auto shader = static_cast<DeviceShaderVK *>(m_imguiMat->getProgram()->getDeviceShader());
        VkDescriptorSetLayout layout = m_imguiPipeline->getDescriptorSetLayOut();
        VkDescriptorSetAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = backEnd->getDescriptorPool();
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &layout;
        auto res = vkAllocateDescriptorSets(backEnd->getDevice(), &alloc_info, &m_imguiDescriptorSet);
        if(res)
        {
            abort();
        }
         printf("create descriptor sets 44444 %p\n", m_imguiDescriptorSet);




        m_imguiUniformBuffer = static_cast<DeviceBufferVK*>(backEnd->createBuffer_imp());
        m_imguiUniformBuffer->init(DeviceBufferType::Uniform);
        m_imguiUniformBuffer->allocateEmpty(sizeof(Matrix44));

        //update descriptor
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_imguiUniformBuffer->getBuffer();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(Matrix44);

        VkWriteDescriptorSet writeSet;
        writeSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeSet.pNext = nullptr;
        writeSet.dstSet = m_imguiDescriptorSet;
        writeSet.dstBinding = 0;
        writeSet.dstArrayElement = 0;
        writeSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writeSet.descriptorCount = 1;
        writeSet.pBufferInfo = &bufferInfo;
        VkWriteDescriptorSet writeSetList[] = {writeSet};
        vkUpdateDescriptorSets(backEnd->getDevice(), 1, writeSetList, 0, nullptr);
    

        ImGuiIO& io = ImGui::GetIO();

        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        m_imguiTextureFont = new DeviceTextureVK();
        m_imguiTextureFont->initDataRaw(pixels, width, height, ImageFormat::R8G8B8A8);
        io.Fonts->TexID = m_imguiTextureFont;
        //m_imguiPipeline->getMaterialDescriptorSet()->updateDescriptorByBinding(1, m_imguiTextureFont);
    }


	void GraphicsRenderer::render()
	{
        auto backEnd = static_cast<VKRenderBackEnd *>(Engine::shared()->getRenderBackEnd());
        auto screenSize = Engine::shared()->winSize();
        backEnd->prepareFrame();
        m_renderPath->prepare();
        //CPU here
        SceneCuller::shared()->collectPrimitives();
        RenderQueues * renderQueues = SceneCuller::shared()->getRenderQueues();
        auto & commonList = renderQueues->getCommonList();
        for (int i = 0 ; i < 3 ; i++)
        {
            auto & shadowList = renderQueues->getShadowList(i);
            m_ShadowStage[i]->prepare();
            m_ShadowStage[i]->beginRenderPass();
            for(auto & command : shadowList)
            {
                if(command.batchType() != RenderCommand::RenderBatchType::Single){
                
                    command.setMat(m_shadowInstancedMat);
                }else
                {
                    command.setMat(m_shadowMat);
                }
                command.m_transInfo.m_viewMatrix = ShadowMap::shared()->getLightViewMatrix();
                command.m_transInfo.m_projectMatrix = ShadowMap::shared()->getLightProjectionMatrix(i);

            }
            //drawObjs_Common(m_matPipelinePool, shadowCommand[i], m_ShadowStage[i], shadowList);
            m_ShadowStage[i]->draw(shadowList);
            m_ShadowStage[i]->endRenderPass();
            m_ShadowStage[i]->finish();
            m_renderPath->addRenderStage(m_ShadowStage[i]);
        }

        //------------deferred g - pass begin-------------
        m_gPassStage->prepare();
        m_gPassStage->beginRenderPass();
        m_gPassStage->draw(commonList);
        m_gPassStage->endRenderPass();
        m_gPassStage->finish();
        m_renderPath->addRenderStage(m_gPassStage);

        //------------deferred g - pass end-----------------

        //------------deferred Lighting Pass begin---------------
        {

            m_DeferredLightingStage->prepare();
            m_DeferredLightingStage->beginRenderPass();
            auto pipeline = m_DeferredLightingStage->getSinglePipeline();
            Matrix44 lightVPList[3] = {};
            float shadowEnd[3] = {};
            for(int i = 0; i < 3; i++)
            {
                shadowEnd[i] =  ShadowMap::shared()->getCascadeEnd(i);
                lightVPList[i] = ShadowMap::shared()->getLightProjectionMatrix(i) * ShadowMap::shared()->getLightViewMatrix();
            }
            pipeline->updateUniformSingle("TU_LightVP",lightVPList, sizeof(lightVPList));
            pipeline->updateUniformSingle("TU_ShadowMapEnd", shadowEnd, sizeof(shadowEnd));


            auto gbufferTex = m_gPassStage->getFrameBuffer()->getTextureList();
            std::vector<VkDescriptorImageInfo> imageInfoList;
            for(int i =0; i < gbufferTex.size(); i++)
            {
                auto tex = gbufferTex[i];
                pipeline->getMaterialDescriptorSet()->updateDescriptorByBinding(i + 1, tex);
            }
            std::vector<DeviceTextureVK *> shadowList = {
                m_ShadowStage[0]->getFrameBuffer()->getDepthMap(),
                m_ShadowStage[1]->getFrameBuffer()->getDepthMap(), 
                m_ShadowStage[2]->getFrameBuffer()->getDepthMap()
            };

            pipeline->getMaterialDescriptorSet()->updateDescriptorByBinding(8, shadowList[0]);
            pipeline->getMaterialDescriptorSet()->updateDescriptorByBinding(9, shadowList[1]);
            pipeline->getMaterialDescriptorSet()->updateDescriptorByBinding(10, shadowList[2]);

            m_DeferredLightingStage->bindSinglePipelineDescriptor();
            m_DeferredLightingStage->drawScreenQuad();

            m_DeferredLightingStage->endRenderPass();
            backEnd->blitTexture(m_DeferredLightingStage->getCommand(),
                m_gPassStage->getFrameBuffer()->getDepthMap(), 
                m_DeferredLightingStage->getFrameBuffer()->getDepthMap(),
                m_DeferredLightingStage->getFrameBuffer()->getSize(), 
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

            m_DeferredLightingStage->finish();
            m_renderPath->addRenderStage(m_DeferredLightingStage);
        }
        //------------deferred Lighting Pass end---------------

        //------------transparent pass begin ------------------
        {
        m_transparentStage->prepare();
        m_transparentStage->beginRenderPass();
        auto transList = renderQueues->getTransparentList();
        m_transparentStage->draw(transList);
        m_transparentStage->endRenderPass();
        m_transparentStage->finish();
        m_renderPath->addRenderStage(m_transparentStage);
        }
        //------------transparent pass end ------------------


        //------------Sky Pass begin---------------
        
        {
            m_skyStage->prepare();
            m_skyStage->beginRenderPass();
            DeviceItemBuffer itemBuf = backEnd->getItemBufferPool()->giveMeItemBuffer(sizeof(Matrix44));
            //update uniform.
            DeviceDescriptorVK * itemDescriptorSet = m_skyStage->getSinglePipeline()->giveItemWiseDescriptorSet();
            itemBuf.map();
            Matrix44 scale;
            scale.setScale(vec3(6360000.0f, 6360000.0f, 6360000.0f));
            Matrix44 m = g_GetCurrScene()->defaultCamera()->getViewProjectionMatrix() * scale;
            itemBuf.copyFrom(&m, sizeof(Matrix44));
            itemBuf.unMap();
            itemDescriptorSet->updateDescriptorByBinding(0, &itemBuf);
            auto tex = m_gPassStage->getFrameBuffer()->getDepthMap();
            m_skyStage->getSinglePipeline()->getMaterialDescriptorSet()->updateDescriptorByBinding(1, tex);
            m_skyStage->bindSinglePipelineDescriptor(itemDescriptorSet);
            m_skyStage->drawSphere();
            m_skyStage->endRenderPass();
            m_skyStage->finish();
            m_renderPath->addRenderStage(m_skyStage);
        }
        //------------Sky Pass end---------------
        {
            m_fogStage->prepare();
            m_fogStage->beginRenderPass();
            auto gbufferTex = m_gPassStage->getFrameBuffer()->getTextureList();
            for(int i =0; i < gbufferTex.size(); i++)
            {
                auto tex = gbufferTex[i];
                m_fogStage->getSinglePipeline()->getMaterialDescriptorSet()->updateDescriptorByBinding(i + 1, tex);
            }
            m_fogStage->bindSinglePipelineDescriptor();
            m_fogStage->drawScreenQuad();
            m_fogStage->endRenderPass();
            m_fogStage->finish();
            m_renderPath->addRenderStage(m_fogStage);
        }

        //------------Texture To Screen Pass begin---------------

        int imageIdx = backEnd->getCurrSwapIndex();
        m_textureToScreenRenderStage[imageIdx]->prepare();
        m_textureToScreenRenderStage[imageIdx]->beginRenderPass();
        auto lightingResultTex = m_fogStage->getFrameBuffer()->getTextureList();
        auto tex = lightingResultTex[0];
        m_textureToScreenRenderStage[imageIdx]->getSinglePipeline()->getMaterialDescriptorSet()->updateDescriptorByBinding(1, tex);
        m_textureToScreenRenderStage[imageIdx]->bindSinglePipelineDescriptor();
        m_textureToScreenRenderStage[imageIdx]->drawScreenQuad();
        m_textureToScreenRenderStage[imageIdx]->endRenderPass();
        m_textureToScreenRenderStage[imageIdx]->finish();
        m_renderPath->addRenderStage(m_textureToScreenRenderStage[imageIdx]);
        //------------Texture To Screen Pass end---------------
		

        //------------GUI Pass begin---------------
        auto drawSize = renderQueues->getGUICommandList().size();
        m_guiStage[imageIdx]->prepare();
        m_guiStage[imageIdx]->beginRenderPass();
        m_guiStage[imageIdx]->draw(renderQueues->getGUICommandList());
        if(!m_imguiPipeline)
        {
            initImguiStuff();
        }
        //IMGUI
        GUISystem::shared()->renderIMGUI();

        auto draw_data = GUISystem::shared()->getDrawData();
        int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
        int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
        // Will project scissor/clipping rectangles into framebuffer space
        ImVec2 clip_off = draw_data->DisplayPos;         // (0,0) unless using multi-viewports
        ImVec2 clip_scale = draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)
        if (draw_data->TotalVtxCount > 0)
        {
            m_imguiUniformBuffer->map();
		        Matrix44 projection;
                auto screenSize = Engine::shared()->winSize();
		        projection.ortho(0.0f, screenSize.x, screenSize.y, 0.0f, 0.1f, 10.0f);
                m_imguiUniformBuffer->copyFrom(&projection, sizeof(Matrix44));
            m_imguiUniformBuffer->unmap();
            // Create or resize the vertex/index buffers
            size_t vertex_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
            size_t index_size = draw_data->TotalIdxCount * sizeof(ImDrawIdx);
            if (m_imguiVertex->getBuffer() == VK_NULL_HANDLE || m_imguiVertex->getSize() < vertex_size)
                m_imguiVertex->allocateEmpty(vertex_size);
                //CreateOrResizeBuffer(rb->VertexBuffer, rb->VertexBufferMemory, rb->VertexBufferSize, vertex_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            if (m_imguiIndex->getBuffer() == VK_NULL_HANDLE || m_imguiIndex->getSize() < index_size)
                m_imguiIndex->allocateEmpty(index_size);
                //CreateOrResizeBuffer(rb->IndexBuffer, rb->IndexBufferMemory, rb->IndexBufferSize, index_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

            // Upload vertex/index data into a single contiguous GPU buffer
            size_t vtx_dst_offset = 0;
            size_t idx_dst_offset = 0;
            m_imguiVertex->map();
            m_imguiIndex->map();
            for (int n = 0; n < draw_data->CmdListsCount; n++)
            {
                const ImDrawList* cmd_list = draw_data->CmdLists[n];
                m_imguiVertex->copyFrom(cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert), vtx_dst_offset);
                m_imguiIndex->copyFrom(cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx), idx_dst_offset);

                vtx_dst_offset += cmd_list->VtxBuffer.Size * sizeof(ImDrawVert);
                idx_dst_offset += cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx);
            }
            VkMappedMemoryRange range[2] = {};
            range[0].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range[0].memory = m_imguiVertex->getMemory();
            range[0].size = VK_WHOLE_SIZE;
            range[1].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range[1].memory = m_imguiIndex->getMemory();
            range[1].size = VK_WHOLE_SIZE;
            VkResult err = vkFlushMappedMemoryRanges(backEnd->getDevice(), 2, range);
            m_imguiVertex->unmap();
            m_imguiIndex->unmap();

            // Render command lists
            // (Because we merged all buffers into a single one, we maintain our own offset into them)
            int global_vtx_offset = 0;
            int global_idx_offset = 0;
            m_imguiPipeline->collcetItemWiseDescritporSet();
            for (int n = 0; n < draw_data->CmdListsCount; n++)
            {
                const ImDrawList* cmd_list = draw_data->CmdLists[n];
                for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
                {
                    
                    const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];


                    auto descriptiorSet = m_imguiPipeline->giveItemWiseDescriptorSet();

                    descriptiorSet->updateDescriptorByBinding(0,m_imguiUniformBuffer,0, sizeof(Matrix44));
                    if(pcmd->TextureId)
                    {
                        descriptiorSet->updateDescriptorByBinding(1,static_cast<DeviceTextureVK * >(pcmd->TextureId));
                    }
                    else
                    {
                        descriptiorSet->updateDescriptorByBinding(1,m_imguiTextureFont);
                    }
                    

                    m_guiStage[imageIdx]->bindPipeline(m_imguiPipeline);

                    std::vector<DeviceDescriptorVK *> descriptorSetList = {m_imguiPipeline->getMaterialDescriptorSet(), descriptiorSet};
                    
                    m_guiStage[imageIdx]->bindDescriptor(m_imguiPipeline, descriptorSetList);

                    m_guiStage[imageIdx]->bindVBO(m_imguiVertex);
                    m_guiStage[imageIdx]->bindIBO(m_imguiIndex);

                    // Project scissor/clipping rectangles into framebuffer space
                    ImVec4 clip_rect;
                    clip_rect.x = (pcmd->ClipRect.x - clip_off.x) * clip_scale.x;
                    clip_rect.y = (pcmd->ClipRect.y - clip_off.y) * clip_scale.y;
                    clip_rect.z = (pcmd->ClipRect.z - clip_off.x) * clip_scale.x;
                    clip_rect.w = (pcmd->ClipRect.w - clip_off.y) * clip_scale.y;

                    if (clip_rect.x < fb_width && clip_rect.y < fb_height && clip_rect.z >= 0.0f && clip_rect.w >= 0.0f)
                    {
                        // Negative offsets are illegal for vkCmdSetScissor
                        if (clip_rect.x < 0.0f)
                            clip_rect.x = 0.0f;
                        if (clip_rect.y < 0.0f)
                            clip_rect.y = 0.0f;

                        // Apply scissor/clipping rectangle
                        vec4 scissorRect;
                        scissorRect.x = clip_rect.x;
                        scissorRect.y = clip_rect.y;
                        scissorRect.z = clip_rect.z - clip_rect.x;
                        scissorRect.w = clip_rect.w - clip_rect.y;
                        m_guiStage[imageIdx]->setScissor(scissorRect);
                        
                    }
                    // Draw
                    m_guiStage[imageIdx]->drawElement(pcmd->ElemCount, 1, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset, 0);
                }
                global_idx_offset += cmd_list->IdxBuffer.Size;
                global_vtx_offset += cmd_list->VtxBuffer.Size;
            }
        }

        m_guiStage[imageIdx]->endRenderPass();
        m_guiStage[imageIdx]->finish();
        m_renderPath->addRenderStage(m_guiStage[imageIdx]);
        //------------GUI Pass end---------------

        renderQueues->clearCommands();

        bool isAnythumbnail = false;
        auto & thumbNailList = Renderer::shared()->getThumbNailList();
	    for(auto thumbnail : thumbNailList)
	    {
		    if(!thumbnail->isIsDone())
		    {
                if(!thumbnail->getFrameBufferVK()){
                    thumbnail->initFrameBufferVK(m_thumbNailRenderStage->getRenderPass());
                }
                
                std::vector<RenderCommand> thumbnailCommandList;
                thumbnail->getSnapShotCommand(thumbnailCommandList);
                m_thumbNailRenderStage->setFrameBuffer(thumbnail->getFrameBufferVK());
                m_thumbNailRenderStage->prepare();
                m_thumbNailRenderStage->beginRenderPass(vec4(0.5, 0.5, 0.5, 1.0));
                m_thumbNailRenderStage->draw(thumbnailCommandList);
                m_thumbNailRenderStage->finish();
                isAnythumbnail = true;
			    thumbnail->setIsDone(true);
                m_renderPath->addRenderStage(m_thumbNailRenderStage);
                break;
		    }
	    }
        backEnd->endFrame(m_renderPath);
	}
}