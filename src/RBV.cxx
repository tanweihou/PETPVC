/*
   RBV.cxx

   Author:      Benjamin A. Thomas

   Copyright 2013 Institute of Nuclear Medicine, University College London.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

   This program implements the region-based voxel-wise (RBV) partial volume
   correction (PVC) technique. The method is described in:

        Thomas, B. and Erlandsson, K. and Modat, M. and Thurfjell, L. and
        Vandenberghe, R. and Ourselin, S. and Hutton, B. (2011). "The importance
        of appropriate partial volume correction for PET quantification in
        Alzheimer's disease". European Journal of Nuclear Medicine and
        Molecular Imaging, 38:1104-1119.

 */

#include "itkImage.h"
#include "itkImageFileReader.h"
#include "itkImageFileWriter.h"
#include <metaCommand.h>

#include "petpvcRBVPVCImageFilter.h"

const char * const VERSION_NO = "0.0.3";
const char * const AUTHOR = "Benjamin A. Thomas";
const char * const APP_TITLE = "Region-based voxel-wise (RBV) PVC";

typedef itk::Vector<float, 3> VectorType;
typedef itk::Image<float, 4> MaskImageType;
typedef itk::Image<float, 3> PETImageType;

typedef itk::ImageFileReader<MaskImageType> MaskReaderType;
typedef itk::ImageFileReader<PETImageType> PETReaderType;
typedef itk::ImageFileWriter<PETImageType> PETWriterType;

//Produces the text for the acknowledgment dialog in Slicer.
std::string getAcknowledgments(void);

int main(int argc, char *argv[])
{

    typedef petpvc::RBVPVCImageFilter<PETImageType, MaskImageType>  FilterType;

    //Setting up command line argument list.
    MetaCommand command;

    command.SetVersion(VERSION_NO);
    command.SetAuthor(AUTHOR);
    command.SetName(APP_TITLE);
    command.SetDescription(
        "Performs Region-based voxel-wise (RBV) partial volume correction");

    std::string sAcks = getAcknowledgments();
    command.SetAcknowledgments(sAcks.c_str());

    command.SetCategory("PETPVC");

    command.AddField("petfile", "PET filename", MetaCommand::IMAGE, MetaCommand::DATA_IN);
    command.AddField("maskfile", "mask filename", MetaCommand::IMAGE, MetaCommand::DATA_IN);
    command.AddField("outputfile", "output filename", MetaCommand::IMAGE, MetaCommand::DATA_OUT);

    command.SetOption("FWHMx", "x", true,
                      "The full-width at half maximum in mm along x-axis");
    command.AddOptionField("FWHMx", "X", MetaCommand::FLOAT, true, "");

    command.SetOption("FWHMy", "y", true,
                      "The full-width at half maximum in mm along y-axis");
    command.AddOptionField("FWHMy", "Y", MetaCommand::FLOAT, true, "");

    command.SetOption("FWHMz", "z", true,
                      "The full-width at half maximum in mm along z-axis");
    command.AddOptionField("FWHMz", "Z", MetaCommand::FLOAT, true, "");

    command.SetOption("debug", "d", false,"Prints debug information");
    command.SetOptionLongTag("debug", "debug");

    //Parse command line.
    if (!command.Parse(argc, argv)) {
        return EXIT_FAILURE;
    }

    //Get image filenames
    std::string sPETFileName = command.GetValueAsString("petfile");
    std::string sMaskFileName = command.GetValueAsString("maskfile");
    std::string sOutputFileName = command.GetValueAsString("outputfile");

    //Get values for PSF.
    float fFWHM_x = command.GetValueAsFloat("FWHMx", "X");
    float fFWHM_y = command.GetValueAsFloat("FWHMy", "Y");
    float fFWHM_z = command.GetValueAsFloat("FWHMz", "Z");

    //Make vector of FWHM in x,y and z.
    VectorType vFWHM;
    vFWHM[0] = fFWHM_x;
    vFWHM[1] = fFWHM_y;
    vFWHM[2] = fFWHM_z;

    //Toggle debug mode
    bool bDebug = command.GetValueAsBool("debug");

    //Create reader for mask image.
    MaskReaderType::Pointer maskReader = MaskReaderType::New();
    maskReader->SetFileName(sMaskFileName);

    //Try to read mask.
    try {
        maskReader->Update();
    } catch (itk::ExceptionObject & err) {
        std::cerr << "[Error]\tCannot read mask input file: " << sMaskFileName
                  << std::endl;
        return EXIT_FAILURE;
    }

    //Create reader for PET image.
    PETReaderType::Pointer petReader = PETReaderType::New();
    petReader->SetFileName(sPETFileName);

    //Try to read PET.
    try {
        petReader->Update();
    } catch (itk::ExceptionObject & err) {
        std::cerr << "[Error]\tCannot read PET input file: " << sPETFileName
                  << std::endl;
        return EXIT_FAILURE;
    }

    //Calculate the variance for a given FWHM.
    VectorType vVariance;
    vVariance = vFWHM / (2.0 * sqrt(2.0 * log(2.0)));
    //std::cout << vVariance << std::endl;

    VectorType vVoxelSize = petReader->GetOutput()->GetSpacing();
    //std::cout << vVoxelSize << std::endl;

    vVariance[0] = pow(vVariance[0], 2);
    vVariance[1] = pow(vVariance[1], 2);
    vVariance[2] = pow(vVariance[2], 2);

    FilterType::Pointer rbvFilter = FilterType::New();
    rbvFilter->SetInput( petReader->GetOutput() );
    rbvFilter->SetMaskInput( maskReader->GetOutput() );
    rbvFilter->SetPSF(vVariance);
    rbvFilter->SetVerbose( bDebug );

    //Perform RBV.
    try {
        rbvFilter->Update();
    } catch (itk::ExceptionObject & err) {
        std::cerr << "\n[Error]\tfailure applying RBV on: " << sPETFileName
                  << "\n" << err
                  << std::endl;
        return EXIT_FAILURE;
    }

    PETWriterType::Pointer petWriter = PETWriterType::New();
    petWriter->SetFileName(sOutputFileName);
    petWriter->SetInput( rbvFilter->GetOutput() );

    try {
        petWriter->Update();
    } catch (itk::ExceptionObject & err) {
        std::cerr << "[Error]\tCannot write output file: " << sOutputFileName
                  << std::endl;

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

std::string getAcknowledgments(void)
{
    //Produces acknowledgments string for 3DSlicer.
    std::string sAck = "This program implements the region-based voxel-wise (RBV) partial volume correction (PVC) technique.\nThe method is described in:\n"
                       "\tThomas, B. and Erlandsson, K. and Modat, M. and Thurfjell, L. and Vandenberghe, R.\n\tand Ourselin, S. and Hutton, B. (2011). \"The importance "
                       "of appropriate partial\n\tvolume correction for PET quantification in Alzheimer\'s disease\".\n\tEuropean Journal of Nuclear Medicine and Molecular Imaging, 38:1104-1119.";
    return sAck;
}
