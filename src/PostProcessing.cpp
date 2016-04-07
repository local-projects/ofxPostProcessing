/*
 *  PostProcessing.cpp
 *
 *  Copyright (c) 2012, Neil Mendoza, http://www.neilmendoza.com
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of Neil Mendoza nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include "PostProcessing.h"
#include "ofMain.h"

namespace itg
{

	void PostProcessing::init(ofFbo::Settings s)
	{
		this->width = s.width;
		this->height = s.width;
		this->arb = s.textureTarget == GL_TEXTURE_RECTANGLE_ARB ;

		raw.allocate(s);

		s.useDepth = false;
		s.depthStencilAsTexture = false;
		// no need to use depth for ping pongs
		for (int i = 0; i < 2; ++i)
		{
			pingPong[i].allocate(s);
		}

		numProcessedPasses = 0;
		currentReadFbo = 0;
		flip = false;
	}

    void PostProcessing::init(unsigned width, unsigned height, bool arb)
    {
        this->width = width;
        this->height = height;
        this->arb = arb;
        
        ofFbo::Settings s;
        
        if (arb)
        {
            s.width = width;
            s.height = height;
            s.textureTarget = GL_TEXTURE_RECTANGLE_ARB;
        }
        else
        {
            s.width = ofNextPow2(width);
            s.height = ofNextPow2(height);
            s.textureTarget = GL_TEXTURE_2D;
        }
        
        // no need to use depth for ping pongs
        for (int i = 0; i < 2; ++i)
        {
            pingPong[i].allocate(s);
        }
        
        s.useDepth = true;
        s.depthStencilInternalFormat = GL_DEPTH_COMPONENT24;
        s.depthStencilAsTexture = true;
        raw.allocate(s);
        
        numProcessedPasses = 0;
        currentReadFbo = 0;
        flip = false;
    }
    
    void PostProcessing::begin()
    {
        raw.begin(false);
        
        ofMatrixMode(GL_PROJECTION);
        ofPushMatrix();
        
        ofMatrixMode(GL_MODELVIEW);
        ofPushMatrix();
        
        glViewport(0, 0, raw.getWidth(), raw.getHeight());
        
        glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
        
        ofPushStyle();
    }
    
    void PostProcessing::begin(ofCamera& cam)
    {
        // update camera matrices
        cam.begin();
        cam.end();
        
        raw.begin(false);
        
        ofMatrixMode(GL_PROJECTION);
        ofPushMatrix();
        
		ofLoadMatrix(cam.getProjectionMatrix(ofRectangle(0, 0, width, height)).getPtr());
        
        ofMatrixMode(GL_MODELVIEW);
        ofPushMatrix();
		ofLoadMatrix(cam.getModelViewMatrix().getPtr());
        
        glViewport(0, 0, raw.getWidth(), raw.getHeight());
        
        glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
        
        ofPushStyle();
    }
    
    void PostProcessing::end(bool autoDraw)
    {
        ofPopStyle();
        
        glViewport(0, 0, ofGetWidth(), ofGetHeight());
        
        ofMatrixMode(GL_PROJECTION);
        ofPopMatrix();
        
        ofMatrixMode(GL_MODELVIEW);
        ofPopMatrix();
        
        raw.end();
        
        ofPushStyle();
        ofSetColor(255, 255, 255);
        process();
        if (autoDraw) draw();
        ofPopStyle();
    }
    
    void PostProcessing::debugDraw()
    {
        raw.getTexture().draw(10, 10, 300, 300);
        raw.getDepthTexture().draw(320, 10, 300, 300);
        pingPong[currentReadFbo].draw(630, 10, 300, 300);
    }
    
    void PostProcessing::draw(float x, float y) const
    {
        draw(x, y, width, height);
    }
    
    void PostProcessing::draw(float x, float y, float w, float h) const
    {
        if (flip)
        {
            ofPushMatrix();
            ofTranslate(0, h, 0);
            ofScale(1, -1, 1);
        }
        if (numProcessedPasses == 0) raw.draw(x, y, w, h);
        else pingPong[currentReadFbo].draw(x, y, w, h);
        if (flip) ofPopMatrix();
    }
    
    ofTexture& PostProcessing::getProcessedTextureReference()
    {
        if (numProcessedPasses) return pingPong[currentReadFbo].getTexture();
        else return raw.getTexture();
    }
    
    // need to have depth enabled for some fx
    void PostProcessing::process(ofFbo& raw, bool hasDepthAsTexture)
    {
        numProcessedPasses = 0;
        for (int i = 0; i < passes.size(); ++i)
        {
            if (passes[i]->getEnabled())
            {
                if (arb && !passes[i]->hasArbShader()) ofLogError() << "Arb mode is enabled but pass " << passes[i]->getName() << " does not have an arb shader.";
                else
                {
                    if (hasDepthAsTexture)
                    {
                        if (numProcessedPasses == 0) passes[i]->render(raw, pingPong[1 - currentReadFbo], raw.getDepthTexture());
                        else passes[i]->render(pingPong[currentReadFbo], pingPong[1 - currentReadFbo], raw.getDepthTexture());
                    }
                    else
                    {
                        if (numProcessedPasses == 0) passes[i]->render(raw, pingPong[1 - currentReadFbo]);
                        else passes[i]->render(pingPong[currentReadFbo], pingPong[1 - currentReadFbo]);
                    }
                    currentReadFbo = 1 - currentReadFbo;
                    numProcessedPasses++;
                }
            }
        }
    }
    
    void PostProcessing::process()
    {
        process(raw);
    }
}
