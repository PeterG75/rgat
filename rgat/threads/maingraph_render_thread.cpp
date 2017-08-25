/*
Copyright 2016-2017 Nia Catlin

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

/*
The thread that performs high (ie:interactive) performance rendering of the selected graph
*/

#include "stdafx.h"
#include "maingraph_render_thread.h"
#include "processLaunching.h"
#include "ui_rgat.h"

void maingraph_render_thread::performMainGraphRendering(plotted_graph *graph)
{
	proto_graph *protoGraph = graph->get_protoGraph();
	if (!protoGraph || protoGraph->edgeList.empty()) return;

	if (!graph->get_mainnodes() || !graph->setGraphBusy(true, 2))
		return;

	//update the render if there are more verts/edges or graph is being resized
	if (
		(graph->get_mainnodes()->get_numVerts() < protoGraph->get_num_nodes()) ||
		(graph->get_mainlines()->get_renderedEdges() < protoGraph->get_num_edges()) ||
		graph->pending_rescale() || graph->vertResizeIndex)
	{
		graph->updateMainRender();
	}
	
	if (protoGraph->active)
	{
		if (graph->isAnimated())
			graph->render_live_animation(clientState->config.animationFadeRate);
		else
			graph->highlight_last_active_node();
	}
	else if (protoGraph->terminated)
	{
		graph->reset_animation();
		protoGraph->terminated = false;
	}

	else if (!protoGraph->active && (graph->replayState == ePlaying || graph->userSelectedAnimPosition != -1))
	{
		graph->render_replay_animation(clientState->config.animationFadeRate);
	}

	graph->setGraphBusy(false, 2);
}

void maingraph_render_thread::main_loop()
{
	alive = true;
	plotted_graph *activeGraph = NULL;
	int renderFrequency = clientState->config.renderFrequency;

	while (!clientState->rgatIsExiting())
	{
		
		activeGraph = (plotted_graph *)clientState->getActiveGraph(false);
		while (!activeGraph || !activeGraph->get_mainlines()) 
		{
			activeGraph = (plotted_graph *)clientState->getActiveGraph(false);
			Sleep(50); continue;
		}

		if (activeGraph->increase_thread_references())
		{
			//cout << "[+1: "<< activeGraph->threadReferences << "]Mainrenderer increased references " <<  endl;
			if (activeGraph->getLayout() != clientState->newGraphLayout)
			{
				activeGraph->setBeingDeleted();
				activeGraph->decrease_thread_references();
				//cout << "[-1: " << activeGraph->threadReferences << "]Mainrenderer a decreased references " << endl;

				clientState->clearActiveGraph();

				traceRecord *activeTrace = (traceRecord *)activeGraph->get_protoGraph()->get_piddata()->tracePtr;

				while (clientState->getActiveGraph(false) == activeGraph)
					Sleep(25);
				activeTrace->graphListLock.lock();
				proto_graph *protoGraph = activeGraph->get_protoGraph();
				
				delete activeGraph;

				activeGraph = (plotted_graph *)clientState->createNewPlottedGraph(protoGraph);
				activeGraph->initialiseDefaultDimensions();
				cout << "created new graph " << activeGraph << endl;

				assert(clientState->setActiveGraph(activeGraph));
				activeTrace->plottedGraphs.at(protoGraph->get_TID()) = activeGraph;

				activeTrace->graphListLock.unlock();

				THREAD_POINTERS *processThreads = (THREAD_POINTERS *)activeTrace->processThreads;
				if (!processThreads->previewThread->is_alive())
				{
					rgat_create_thread((LPTHREAD_START_ROUTINE)processThreads->previewThread->ThreadEntry, processThreads->previewThread);
				}

				if (!processThreads->conditionalThread->is_alive())
				{
					rgat_create_thread((LPTHREAD_START_ROUTINE)processThreads->conditionalThread->ThreadEntry, processThreads->conditionalThread);
				}

				if (!processThreads->heatmapThread->is_alive())
				{
					rgat_create_thread((LPTHREAD_START_ROUTINE)processThreads->heatmapThread->ThreadEntry, processThreads->heatmapThread);
				}
				continue;
			}

			performMainGraphRendering(activeGraph);
			activeGraph->decrease_thread_references();
			//cout << "[-1: " << activeGraph->threadReferences << "]Mainrenderer  b decreased references " << endl;
		}
		activeGraph = NULL;

		Sleep(renderFrequency);
	}
	alive = false;
}