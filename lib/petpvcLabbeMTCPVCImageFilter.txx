/*
   petpvcLabbeMTCPVCImageFilter.txx

   Author:      Benjamin A. Thomas

   Copyright 2015 Institute of Nuclear Medicine, University College London.

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

#ifndef __PETPVCLABBEMTCPVCImageFilter_TXX
#define __PETPVCLABBEMTCPVCImageFilter_TXX

#include "petpvcLabbeMTCPVCImageFilter.h"
#include "itkObjectFactory.h"
#include "itkImageRegionIterator.h"
#include "itkImageRegionConstIterator.h"

using namespace itk;

namespace petpvc
{

template< class TInputImage, class TMaskImage >
LabbeMTCPVCImageFilter< TInputImage, TMaskImage>
::LabbeMTCPVCImageFilter()
{
    this->m_bVerbose = false;
}


template< class TInputImage, class TMaskImage >
void LabbeMTCPVCImageFilter< TInputImage, TMaskImage>
::GenerateData()
{
    typename TInputImage::ConstPointer input = this->GetInput();
    typename TInputImage::Pointer output = this->GetOutput();

    typename LabbeImageFilterType::Pointer pLabbe = LabbeImageFilterType::New();

    InputImagePointer pPET = dynamic_cast<const TInputImage*> (ProcessObject::GetInput(0));
    MaskImagePointer pMask = dynamic_cast<const TMaskImage*> (ProcessObject::GetInput(1));

    pLabbe->SetInput( pMask );
    pLabbe->SetPSF( this->GetPSF() );
    //Calculate Labbe.
    try {
        pLabbe->Update();
    } catch (itk::ExceptionObject & err) {
        std::cerr << "[Error]\tCannot calculate Labbe"
                  << std::endl;
    }


    //if ( this->m_bVerbose ) {
    //    std::cout << pLabbe->GetMatrix() << std::endl;
    //}

    //Get mask image size.
    typename MaskImageType::SizeType imageSize =
        pMask->GetLargestPossibleRegion().GetSize();

    int nClasses = 0;

    //If mask is not 4D, then quit.
    if (imageSize.Dimension == 4) {
        nClasses = imageSize[3];
    } else {
        std::cerr << "[Error]\tMask file must be 4-D!"
                  << std::endl;
    }

    MaskSizeType desiredStart;
    desiredStart.Fill(0);
    MaskSizeType desiredSize = imageSize;

    //Extract filter used to extract 3D volume from 4D file.
    typename ExtractFilterType::Pointer extractFilter = ExtractFilterType::New();
    extractFilter->SetInput( pMask );
    extractFilter->SetDirectionCollapseToIdentity(); // This is required.

    typename ExtractFilterType::Pointer extractIFilter = ExtractFilterType::New();
    extractIFilter->SetInput( pMask );
    extractIFilter->SetDirectionCollapseToIdentity(); // This is required.

    typename ExtractFilterType::Pointer extractJFilter = ExtractFilterType::New();
    extractJFilter->SetInput( pMask );
    extractJFilter->SetDirectionCollapseToIdentity(); // This is required.

    //Stats. filter used to calculate statistics for an image.
    typename StatisticsFilterType::Pointer statsFilter = StatisticsFilterType::New();

    //Multiplies two images together.
    typename MultiplyFilterType::Pointer multiplyFilter = MultiplyFilterType::New();

    typename TInputImage::Pointer imageExtractedRegion;// = InputImagePointer::New();

    float fSumOfPETReg;

    //Vector to contain the current estimate of the regional mean values.
    vnl_vector<float> vecRegMeansCurrent;
    vecRegMeansCurrent.set_size(nClasses);

    //Vector to contain the estimated means after Labbe correction.
    vnl_vector<float> vecRegMeansUpdated;
    vecRegMeansUpdated.set_size(nClasses);

    typename BlurringFilterType::Pointer gaussFilter = BlurringFilterType::New();
    gaussFilter->SetVariance( this->GetPSF() );

    for (int i = 1; i <= nClasses; i++) {

        //Starts reading from 4D volume at index (0,0,0,i) through to
        //(maxX, maxY, maxZ,0), i.e. one 3D brain mask.
        desiredStart[3] = i - 1;
        desiredSize[3] = 0;

        //Get region mask.
        MaskRegionType maskReg;
        maskReg.SetSize(desiredSize );
        maskReg.SetIndex(0,desiredStart[0] );
        maskReg.SetIndex(1,desiredStart[1] );
        maskReg.SetIndex(2,desiredStart[2] );
        maskReg.SetIndex(3,desiredStart[3] );

        extractFilter->SetExtractionRegion( maskReg );
        extractFilter->Update();

	gaussFilter->SetInput( extractFilter->GetOutput() );
	gaussFilter->Update();

        imageExtractedRegion = gaussFilter->GetOutput();
        imageExtractedRegion->SetDirection( pPET->GetDirection() );
        imageExtractedRegion->UpdateOutputData();

        //Multiply current image estimate by region mask. To clip PET values
        //to mask.
        multiplyFilter->SetInput1( pPET );
        multiplyFilter->SetInput2( imageExtractedRegion );

        statsFilter->SetInput(multiplyFilter->GetOutput());
        statsFilter->Update();

        //Get sum of the clipped image.
        fSumOfPETReg = statsFilter->GetSum();

        //Place regional mean into vector.
        vecRegMeansCurrent.put(i - 1, fSumOfPETReg / pLabbe->GetSumOfRegions().get(i - 1));
        //std::cout << "Sum = " << fSumOfPETReg << " , Mean = " << vecRegMeansCurrent.get(i-1) << " Total vox. = " << LabbeFilter->GetSumOfRegions().get( i-1 ) << std::endl;

    }

    //Apply Labbe to regional mean values.
    vecRegMeansUpdated = vnl_matrix_inverse<float>(pLabbe->GetMatrix()) * vecRegMeansCurrent;

    if ( this->m_bVerbose ) {
        std::cout << std::endl << "Regional means:" << std::endl;
        std::cout << vecRegMeansCurrent << std::endl << std::endl;

        std::cout << "Labbe:" << std::endl;
        pLabbe->GetMatrix().print(std::cout);

        std::cout << std::endl << "Corrected means:" << std::endl;
        std::cout << vecRegMeansUpdated << std::endl;
    }


    //Applying the MTC correction step:

    typename TInputImage::Pointer imageCorrected;
   
    typename AddFilterType::Pointer addFilter = AddFilterType::New();
	typename AddFilterType::Pointer addFilter2 = AddFilterType::New();

    typename BlurringFilterType::Pointer blurFilter = BlurringFilterType::New();
    blurFilter->SetVariance( this->GetPSF() );

    typename BlurringFilterType::Pointer blurFilter2 = BlurringFilterType::New();
    blurFilter2->SetVariance( this->GetPSF() );

    for (int j = 1; j <= nClasses; j++) {
        typename TInputImage::Pointer imageNeighbours = TInputImage::New();

        //Starts reading from 4D volume at index (0,0,0,i) through to
        //(maxX, maxY, maxZ,0), i.e. one 3D brain mask.
        desiredStart[3] = j - 1;
        desiredSize[3] = 0;

        //Get region mask.
        MaskRegionType maskReg;
        maskReg.SetSize(desiredSize );
        maskReg.SetIndex(0,desiredStart[0] );
        maskReg.SetIndex(1,desiredStart[1] );
        maskReg.SetIndex(2,desiredStart[2] );
        maskReg.SetIndex(3,desiredStart[3] );

        extractJFilter->SetExtractionRegion( maskReg );
        extractJFilter->Update();

        typename TInputImage::Pointer imageRegionJ;
        imageRegionJ = extractJFilter->GetOutput();
        imageRegionJ->SetDirection( pPET->GetDirection() );
        imageRegionJ->UpdateOutputData();
    
        int neighbourCount = 0;

        for (int i = 1; i <= nClasses; i++) {
            
            if ( j != i ) {

                desiredStart[3] = i - 1;
                maskReg.SetSize(desiredSize );
                maskReg.SetIndex(0,desiredStart[0] );
                maskReg.SetIndex(1,desiredStart[1] );
                maskReg.SetIndex(2,desiredStart[2] );
                maskReg.SetIndex(3,desiredStart[3] );

                extractIFilter->SetExtractionRegion( maskReg );
                extractIFilter->Update();

                typename TInputImage::Pointer imageRegionI;
                imageRegionI = extractIFilter->GetOutput();
                imageRegionI->SetDirection( pPET->GetDirection() );
                imageRegionI->UpdateOutputData();
                
                multiplyFilter->SetInput1( vecRegMeansUpdated.get(i-1) );
                multiplyFilter->SetInput2( imageRegionI );
                //multiplyFilter->Update();

        	blurFilter->SetInput( multiplyFilter->GetOutput() );
	        blurFilter->Update();

                //If this is the first neighbour region, create imageNeighbours,
                //else add the current region to the previous contents of imageNeighbours.
                if (neighbourCount == 0) {
                    imageNeighbours = blurFilter->GetOutput();
                    imageNeighbours->DisconnectPipeline();
                } else {
                    addFilter->SetInput1( imageNeighbours );
                    addFilter->SetInput2( blurFilter->GetOutput() );
                    addFilter->Update();

                    imageNeighbours = addFilter->GetOutput();
                }
                
                neighbourCount++;
            }
        }

	

        typename SubtractFilterType::Pointer subFilter = SubtractFilterType::New();
        subFilter->SetInput1( pPET );
        subFilter->SetInput2( imageNeighbours );
        subFilter->Update();

        blurFilter2->SetInput( imageRegionJ );
        blurFilter2->Update();

        typename DivideFilterType::Pointer divideFilter = DivideFilterType::New();
        divideFilter->SetInput1( subFilter->GetOutput() );
        divideFilter->SetInput2( blurFilter2->GetOutput() );
        divideFilter->Update();

        multiplyFilter->SetInput1( imageRegionJ );
        multiplyFilter->SetInput2( divideFilter->GetOutput() );
        multiplyFilter->Update();
   
        //If this is the first region, create imageCorrected,
        //else add the current region to the previous contents of imageCorrected.
        if (j == 1) {
            imageCorrected = multiplyFilter->GetOutput();
            imageCorrected->DisconnectPipeline();
        } else {
            addFilter2->SetInput1( imageCorrected );
            addFilter2->SetInput2( multiplyFilter->GetOutput());
            addFilter2->Update();

            imageCorrected = addFilter2->GetOutput();
        }

        
        }

    this->AllocateOutputs();

    ImageAlgorithm::Copy( imageCorrected.GetPointer(), output.GetPointer(), output->GetRequestedRegion(),
                          output->GetRequestedRegion() );


}

}// end namespace


#endif
