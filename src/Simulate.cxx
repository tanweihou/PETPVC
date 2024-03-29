/*
   Simulate.cxx

   Author:      Kris Thielemans

   Copyright 2014 Institute of Nuclear Medicine, University College London.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

   This program implements blurring if an image with a kernel to simulate
   the partial volume effect. It is useful for testing the PVC methods.
 */


#include <string>
#include <itkImage.h>
#include <itkImageFileReader.h>
#include <itkImageFileWriter.h>
#include <itkDiscreteGaussianImageFilter.h>

#include <metaCommand.h>
#include <vnl/vnl_vector.h>

const char * const VERSION_NO = "0.0.3";
const char * const AUTHOR = "Kris Thielemans";
const char * const APP_TITLE = "PVE Simulation";

typedef itk::Vector<float, 3> VectorType;
typedef itk::Image<float, 3> PETImageType;

typedef itk::ImageFileReader<PETImageType> PETReaderType;
typedef itk::ImageFileWriter<PETImageType> PETWriterType;
typedef itk::DiscreteGaussianImageFilter<PETImageType, PETImageType> BlurringFilterType;

//Produces the text for the acknowledgments dialog in Slicer.
std::string getAcknowledgments(void);

int main(int argc, char *argv[])
{

    //Setting up command line argument list.
    MetaCommand command;

    command.SetVersion(VERSION_NO);
    command.SetAuthor(AUTHOR);
    command.SetName(APP_TITLE);
    command.SetDescription(
        "Performs PV simulation by simple blurring");

    std::string sAcks = getAcknowledgments();
    command.SetAcknowledgments(sAcks.c_str());

    command.SetCategory("PETPVC");

    command.AddField("petfile", "PET filename", MetaCommand::IMAGE, MetaCommand::DATA_IN);
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

    //Parse command line.
    if (!command.Parse(argc, argv)) {
        return EXIT_FAILURE;
    }

    //Get image filenames
    std::string sPETFileName = command.GetValueAsString("petfile");
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

    //Create blurring filter to apply PSF.
    BlurringFilterType::Pointer blurFilter = BlurringFilterType::New();
    blurFilter->SetVariance(vVariance);

    // apply blur to input image
    blurFilter->SetInput(petReader->GetOutput());

    //Write out result of final iteration.
    PETWriterType::Pointer petWriter = PETWriterType::New();
    petWriter->SetFileName(sOutputFileName);
    petWriter->SetInput(blurFilter->GetOutput());

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
    std::string sAck = "This program implements blurring to simulate the PVE. Please cite the following paper:\n"
                       "\tErlandsson, K. and Buvat, I. and Pretorius, P.H. and Thomas, B.A. and Hutton, B.F., (2012).\n\t\"A review of partial volume correction techniques "
                       "for emission tomography and their applications in neurology, cardiology and oncology\", \n\tPhysics in Medicine and Biology, vol. 57, no. 21, R119-59.";

    return sAck;
}

